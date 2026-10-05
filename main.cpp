#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <queue>
#include <unordered_map>

using Instruction = std::unordered_map<std::string, std::string>;
using InstructionQueue = std::queue<Instruction>;

using ReservationStation = std::vector<Instruction>;
using RegisterStatus = std::unordered_map<std::string, std::string>;

const std::size_t RS_MAX_SIZE = 32;

void fetchAndDecode(InstructionQueue &instructionQueue, std::ifstream &file, int fetch_len)
{
    std::string line;
    int nextTag;

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

        InstructionQueue instr;
        std::string op = tokenList[0];
        instr["op"] = op;

        if (op == "BCN")
        {
            instr["dest"] = "";
            instr["src1"] = (tokenList.size() > 1) ? tokenList[1] : "";
            instr["src2"] = (tokenList.size() > 2) ? tokenList[2] : "";
        }
        else if (op == "STW")
        {
            instr["dest"] = "";
            instr["src1"] = (tokenList.size() > 3) ? tokenList[3] : "";
            instr["src2"] = (tokenList.size() > 4) ? tokenList[4] : "";
        }
        else if (op == "ALU" || op == "BRA")
        {
            instr["dest"] = (tokenList.size() > 2) ? tokenList[2] : "";

            if (tokenList.size() > 4)
            {
                instr["src1"] = tokenList[4];
            }
            else
            {
                instr["src1"] = "";
            }
            instr["src2"] = "";
        }

        instructionQueue.push(instr);
    }

    // Optional: Do something with your instructionQueue here!
    std::cout << "Successfully fetched and decoded instructions.\n";
}

void issue(InstructionQueue &instructionQueue, ReservationStation &reservationStation, int width, RegisterStatus& registerStatus, int& nextTag)
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

        if (instruction["op"] == "ALU")
        {
            // Reservation station is full
            if (reservationStation.size() >= RS_MAX_SIZE)
            {
                return;
            }

            instruction["tag"] = "T" + std::to_string(nextTag++);
            instruction["src1Tag"] = "";
            instruction["src2Tag"] = "";
            instruction["src1Ready"] = "";
            instruction["src2Ready"] = "";

            // Check src1
            // Check src2
            // Set READY/WAITING
            // Update destination’s producer tag
            // Push into reservation station
            // Pop from instruction queue

            reservationStation.push_back(instruction);
            instructionQueue.pop();
        }
        else if (instruction["op"] == "LDW")
        {
            // Load Buffer
        }
        else if (instruction["op"] == "STW")
        {
            // Store Buffer
        }
        else if (instruction["op"] == "BRA" || instruction["op"] == "BCN" || instruction["op"] == "BUC")
        {
            // Branch handling
        }
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
    RegisterStatus registerStatus;

    int width = 4;
    fetchAndDecode(instructionQueue, file, width); // Works perfectly now
    issue(instructionQueue, reservationStation, width, registerStatus, nextTag);
    // dispatch();
    // exectue();
    // writeBack();
    return 0;
}
