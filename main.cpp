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
// using LoadQueue          = std::vector<RSEntry>;  // capacity 8
// using StoreQueue         = std::vector<RSEntry>;  // capacity 8

// using Instruction = std::unordered_map<std::string, std::string>;
using InstructionQueue = std::queue<Instruction>;

const std::size_t RS_MAX_SIZE = 32;

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
            continue;

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
                    instr.dest = std::stoi(tokenList[t].substr(1)); // "R12" -> 12
                else if (srcCount < 2)
                    instr.src[srcCount++] = std::stoi(tokenList[t].substr(1));
            }
        }

        instructionQueue.push(instr);
    }

    // Optional: Do something with your instructionQueue here!
    std::cout << "Successfully fetched and decoded instructions.\n";
}

void issue(InstructionQueue &instructionQueue, ReservationStation &reservationStation, int width, int registerStatus[], int& nextTag)
{

    for (int i = 0; i < width; i++)
    {
        // Nothing available to issue
        if (instructionQueue.empty())
        {
            return;
        }

        Instruction instruction = instructionQueue.front();

        // if the instr is ALU then:

        if (instruction.op == Op::ALU)
        {
            // Reservation station is full
            if (reservationStation.size() >= RS_MAX_SIZE)
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
            entry.cyclesLeft = 2;      // ALU latency (assumed 2 cycle)

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

            // Push into reservation station
            reservationStation.push_back(entry);

            // Pop from instruction queue
            instructionQueue.pop();
        }
        else if (instruction.op == Op::LDW)
        {
            // Load Buffer
        }
        else if (instruction.op == Op::STW)
        {
            // Store Buffer
        }
        else if (instruction.op == Op::BRA || instruction.op == Op::BCN || instruction.op == Op::BUC)
        {
            // Branch handling
        }
    }
}

// Start executing every waiting entry whose operands are both ready.
// There are unlimited functional units, so nothing else limits dispatch.
void dispatch(ReservationStation &reservationStation)
{
    for (RSEntry &entry : reservationStation)
    {
        if (entry.state == State::Waiting && entry.srcTag[0] == -1 && entry.srcTag[1] == -1)
        {
            entry.state = State::Executing;
        }
    }
}

// Advance every executing entry by one cycle. Entries that finish are marked
// Done for write back. Returns how many finished this cycle (used for IPC)
int execute(ReservationStation &reservationStation)
{
    int finished = 0;

    for (RSEntry &entry : reservationStation)
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

// Retire every Done entry: broadcast its tag to waiting entries, release its
// destination register and free its reservation station slot.
// Not limited by the CDB, so every finished entry retires this cycle.
void writeBack(ReservationStation &reservationStation, int registerStatus[])
{
    for (const RSEntry &done : reservationStation)
    {
        if (done.state != State::Done)
            continue;

        // Broadcast: wake up every entry waiting on this tag
        for (RSEntry &entry : reservationStation)
        {
            if (entry.srcTag[0] == done.tag)
                entry.srcTag[0] = -1;
            if (entry.srcTag[1] == done.tag)
                entry.srcTag[1] = -1;
        }

        // Only clear the register if no later instruction has renamed it since
        if (done.dest != -1 && registerStatus[done.dest] == done.tag)
            registerStatus[done.dest] = -1;
    }

    // Remove the retired entries from the reservation station
    reservationStation.erase(
        std::remove_if(reservationStation.begin(), reservationStation.end(),
                       [](const RSEntry &entry) { return entry.state == State::Done; }),
        reservationStation.end());
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
    
    // Tag of the in-flight instruction producing each register, -1 = value is ready
    int registerStatus[128];
    for (int r = 0; r < 128; r++)
        registerStatus[r] = -1;

    int nextTag = 1;

    int width = 4;
    fetchAndDecode(instructionQueue, file, width); // Works perfectly now
    issue(instructionQueue, reservationStation, width, registerStatus, nextTag);
    dispatch(reservationStation);
    int finished = execute(reservationStation);
    writeBack(reservationStation, registerStatus);
    return 0;
}
