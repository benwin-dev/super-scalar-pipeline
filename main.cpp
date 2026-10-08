#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>


enum class Op { ALU, FPU, LDW, STW, BRA, BUC, BCN, CAL, RET };

struct Instruction {
    Op  op;
    int dest;      // register number, or -1 if none
    int src[2];    // register numbers, or -1 if unused
};

enum class State { Waiting, Ready, Executing, Done };

struct RSEntry {
    int   tag;          // unique ID for this instruction
    Op    op;
    int   dest;         // destination register (-1 if none)
    int   srcTag[2];    // Qj, Qk: tag being waited on, -1 = operand ready
    State state;
    int   cyclesLeft;   // remaining execution latency
};

using ReservationStation = std::vector<RSEntry>;  // capacity 32
using LoadQueue          = std::vector<RSEntry>;  // capacity 8
using StoreQueue         = std::vector<RSEntry>;  // capacity 8

// using Instruction = std::unordered_map<std::string, std::string>;
using InstructionQueue = std::queue<Instruction>;

const std::size_t RS_MAX_SIZE = 32;
const std::size_t LQ_MAX_SIZE = 8;
const std::size_t SQ_MAX_SIZE = 8;

// Execution latency in cycles for each operation.
// The spec doesn't give these, so they are assumed values.
int latency(Op op)
{
    switch (op)
    {
    case Op::ALU: return 2;
    case Op::FPU: return 4;
    case Op::LDW: return 3;
    case Op::STW: return 2;
    default:      return 1;   // BRA, BUC, BCN, CAL, RET
    }
}

// Convert an opcode string to Op. Returns false for unknown opcodes.
bool parseOp(const std::string &s, Op &op)
{
    static const std::unordered_map<std::string, Op> ops = {
        {"ALU", Op::ALU}, {"FPU", Op::FPU}, {"LDW", Op::LDW},
        {"STW", Op::STW}, {"BRA", Op::BRA}, {"BUC", Op::BUC},
        {"BCN", Op::BCN}, {"CAL", Op::CAL}, {"RET", Op::RET}};

    auto it = ops.find(s);
    if (it == ops.end())
        return false;
    op = it->second;
    return true;
}

void fetchAndDecode(InstructionQueue &instructionQueue, std::ifstream &file, int fetch_len)
{
    std::string line;

    for (int i = 0; i < fetch_len && std::getline(file, line); i++)
    {
        if (line.empty())
            continue; // do we need to do i-- here since one itr was wasted?

        std::stringstream lineStream(line);
        std::vector<std::string> tokenList;
        std::string token;

        while (lineStream >> token)
        {
            tokenList.push_back(token);
        }

        if (tokenList.empty())
            continue;

        Instruction instr;
        if (!parseOp(tokenList[0], instr.op))
            continue;

        // initialize it as available
        instr.dest = -1;
        instr.src[0] = -1;
        instr.src[1] = -1;

        // BCN/BUC lines hold PC addresses, not registers, so they have no operands
        if (instr.op != Op::BCN && instr.op != Op::BUC)
        {
            // Format: OP DEST [reg] SRC [reg] [reg]
            // Registers after DEST are destinations, registers after SRC are sources
            bool inSrc = false;
            int srcCount = 0;

            for (std::size_t t = 1; t < tokenList.size(); t++)
            {
                if (tokenList[t] == "DEST")
                    inSrc = false;
                else if (tokenList[t] == "SRC")
                    inSrc = true;
                else if (!inSrc)
                    instr.dest = std::stoi(tokenList[t].substr(1));
                else if (srcCount < 2)
                    instr.src[srcCount++] = std::stoi(tokenList[t].substr(1));
            }
        }

        instructionQueue.push(instr);
    }

    // Optional: Do something with your instructionQueue here!
    std::cout << "Successfully fetched and decoded instructions.\n";
}

void issue(InstructionQueue &instructionQueue, ReservationStation &reservationStation,
           LoadQueue &loadQueue, StoreQueue &storeQueue,
           int width, int registerStatus[], int& nextTag)
{

    for (int i = 0; i < width; i++)
    {
        // Nothing available to issue
        if (instructionQueue.empty())
        {
            return;
        }

        Instruction instruction = instructionQueue.front();

        // Pick the structure this instruction goes into:
        // loads -> load queue, stores -> store queue,
        // everything else (ALU, FPU, branches, CAL, RET) -> reservation station
        std::vector<RSEntry> *target;
        std::size_t maxSize;

        if (instruction.op == Op::LDW)
        {
            target = &loadQueue;
            maxSize = LQ_MAX_SIZE;
        }
        else if (instruction.op == Op::STW)
        {
            target = &storeQueue;
            maxSize = SQ_MAX_SIZE;
        }
        else
        {
            target = &reservationStation;
            maxSize = RS_MAX_SIZE;
        }

        // Target is full: stall. Issue is in order, so nothing behind it can issue either
        if (target->size() >= maxSize)
        {
            return;
        }

        RSEntry entry;
        entry.tag = nextTag++;
        entry.op = instruction.op;
        entry.dest = instruction.dest;
        entry.srcTag[0] = -1;      // filled in by the dependency check below
        entry.srcTag[1] = -1;
        entry.state = State::Waiting;
        entry.cyclesLeft = latency(instruction.op);

        // Check src1: wait on its producer's tag, or -1 if the value is already ready
        if (instruction.src[0] != -1)
            entry.srcTag[0] = registerStatus[instruction.src[0]];

        // Check src2
        if (instruction.src[1] != -1)
            entry.srcTag[1] = registerStatus[instruction.src[1]];

        // Update destination’s producer tag (must come after the source checks,
        // so e.g. R4 = R4 + R1 waits on the previous producer of R4, not itself)
        if (instruction.dest != -1)
            registerStatus[instruction.dest] = entry.tag;

        // Push into the chosen structure
        target->push_back(entry);

        // Pop from instruction queue
        instructionQueue.pop();
    }
}

// Start executing every waiting entry whose operands are both ready.
// There are unlimited functional units, so nothing else limits dispatch.
// Called once each for the reservation station, load queue and store queue.
void dispatch(std::vector<RSEntry> &structure)
{
    for (RSEntry &entry : structure)
    {
        if (entry.state == State::Waiting && entry.srcTag[0] == -1 && entry.srcTag[1] == -1)
        {
            entry.state = State::Executing;
        }
    }
}

// Advance every executing entry by one cycle. Entries that finish are marked
// Done for write back. Returns how many finished this cycle (used for IPC)
// Called once each for the reservation station, load queue and store queue.
int execute(std::vector<RSEntry> &structure)
{
    int finished = 0;

    for (RSEntry &entry : structure)
    {
        if (entry.state == State::Executing)
        {
            entry.cyclesLeft--;

            if (entry.cyclesLeft == 0)
            {
                entry.state = State::Done;
                finished++;
            }
        }
    }

    return finished;
}

// Wake up every entry in one structure that is waiting on this tag.
void broadcast(std::vector<RSEntry> &structure, int tag)
{
    for (RSEntry &entry : structure)
    {
        if (entry.srcTag[0] == tag)
            entry.srcTag[0] = -1;
        if (entry.srcTag[1] == tag)
            entry.srcTag[1] = -1;
    }
}

// Retire every Done entry in all three structures: broadcast its tag to waiting
// entries everywhere, release its destination register and free its slot.
// Not limited by the CDB, so every finished entry retires this cycle.
void writeBack(ReservationStation &reservationStation, LoadQueue &loadQueue,
               StoreQueue &storeQueue, int registerStatus[])
{
    std::vector<RSEntry> *structures[] = {&reservationStation, &loadQueue, &storeQueue};

    for (std::vector<RSEntry> *structure : structures)
    {
        for (const RSEntry &done : *structure)
        {
            if (done.state != State::Done)
                continue;

            // Broadcast to all structures, e.g. a finished load must wake ALU ops
            broadcast(reservationStation, done.tag);
            broadcast(loadQueue, done.tag);
            broadcast(storeQueue, done.tag);

            // Only clear the register if no later instruction has renamed it since
            if (done.dest != -1 && registerStatus[done.dest] == done.tag)
                registerStatus[done.dest] = -1;
        }
    }

    // Remove the retired entries from every structure
    for (std::vector<RSEntry> *structure : structures)
    {
        structure->erase(
            std::remove_if(structure->begin(), structure->end(),
                           [](const RSEntry &entry) { return entry.state == State::Done; }),
            structure->end());
    }
}

int main()
{
    std::ifstream file("sample.trace");
    if (!file.is_open())
    {
        std::cerr << "Failed to open file!\n";
        return 1;
    }

    InstructionQueue instructionQueue;
    ReservationStation reservationStation;
    LoadQueue loadQueue;
    StoreQueue storeQueue;

    // Tag of the in-flight instruction producing each register, -1 = value is ready
    int registerStatus[128];
    for (int r = 0; r < 128; r++)
        registerStatus[r] = -1;

    int nextTag = 1;

    int width = 4;
    fetchAndDecode(instructionQueue, file, width); // Works perfectly now
    issue(instructionQueue, reservationStation, loadQueue, storeQueue, width, registerStatus, nextTag);
    dispatch(reservationStation);
    dispatch(loadQueue);
    dispatch(storeQueue);
    execute(reservationStation);
    execute(loadQueue);
    execute(storeQueue);
    writeBack(reservationStation, loadQueue, storeQueue, registerStatus);
    return 0;
}
