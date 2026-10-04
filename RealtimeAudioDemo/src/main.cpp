#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <windows.h>

void printHeader() {
    std::cout << "\n";
    std::cout << "========================================================\n";
    std::cout << "    REAL-TIME AUDIO DEMONSTRATION: TCP vs UDP           \n";
    std::cout << "========================================================\n";
    std::cout << " This application demonstrates the practical audible     \n";
    std::cout << " difference between TCP stream delivery and UDP datagram \n";
    std::cout << " audio streaming using real-time Windows audio playback. \n";
    std::cout << "========================================================\n";
}

void printMenu() {
    std::cout << "\nSelect Demonstration Mode:\n";
    std::cout << "  1. TCP Audio Streaming (Reliable, ordered, 0% loss)\n";
    std::cout << "  2. UDP Audio Streaming (0% Loss - baseline real-time)\n";
    std::cout << "  3. UDP Audio Streaming with  5% Loss (Subtle clicks/gaps)\n";
    std::cout << "  4. UDP Audio Streaming with 10% Loss (Noticeable dropouts)\n";
    std::cout << "  5. UDP Audio Streaming with 20% Loss (Heavy stutter/loss)\n";
    std::cout << "  6. TCP vs UDP Comprehensive Comparison Table\n";
    std::cout << "  7. Exit\n";
    std::cout << "\nEnter Choice [1-7]: ";
}

bool launchProcess(const std::string& cmd, PROCESS_INFORMATION& pi, bool newConsole = false) {
    STARTUPINFOA si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    std::memset(&pi, 0, sizeof(pi));

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    DWORD creationFlags = newConsole ? CREATE_NEW_CONSOLE : 0;

    BOOL success = CreateProcessA(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        FALSE,
        creationFlags,
        NULL,
        NULL,
        &si,
        &pi
    );

    return (success == TRUE);
}

void runDemo(const std::string& serverCmd, const std::string& clientCmd, const std::string& demoTitle) {
    std::cout << "\n--------------------------------------------------------\n";
    std::cout << " Starting Demo: " << demoTitle << "\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "[Orchestrator] Launching audio receiver server...\n";

    PROCESS_INFORMATION serverPi;
    if (!launchProcess(serverCmd, serverPi, true)) {
        std::cerr << "[ERROR] Failed to start server process: " << serverCmd << "\n";
        return;
    }

    // Give server time to bind socket and open audio device
    Sleep(800);

    std::cout << "[Orchestrator] Launching audio sender client...\n";
    PROCESS_INFORMATION clientPi;
    if (!launchProcess(clientCmd, clientPi, false)) {
        std::cerr << "[ERROR] Failed to start client process: " << clientCmd << "\n";
        TerminateProcess(serverPi.hProcess, 0);
        CloseHandle(serverPi.hProcess);
        CloseHandle(serverPi.hThread);
        return;
    }

    // Wait for client to complete transmission
    WaitForSingleObject(clientPi.hProcess, INFINITE);
    CloseHandle(clientPi.hProcess);
    CloseHandle(clientPi.hThread);

    // Allow receiver to finish playing buffered audio
    WaitForSingleObject(serverPi.hProcess, 10000);
    CloseHandle(serverPi.hProcess);
    CloseHandle(serverPi.hThread);

    std::cout << "\n[Orchestrator] " << demoTitle << " session finished.\n";
}

void printComparisonTable() {
    std::cout << "\n=========================================================================================\n";
    std::cout << "                 TCP vs UDP REAL-TIME AUDIO COMPARISON ANALYSIS                          \n";
    std::cout << "=========================================================================================\n";
    std::cout << " Metric / Feature       | TCP Mode                        | UDP Mode                     \n";
    std::cout << "------------------------+---------------------------------+------------------------------\n";
    std::cout << " Connection Protocol    | Connection-oriented (Handshake) | Connectionless (No handshake)\n";
    std::cout << " Port Default           | 6000                            | 6001                         \n";
    std::cout << " Stream Integrity       | 100% Guaranteed by OS kernel    | Best-effort (Loss tolerated) \n";
    std::cout << " Packet Loss Action     | Automatic retransmission (ACKs) | Missing packets skipped      \n";
    std::cout << " Delivery Order         | In-order sequence guaranteed    | Out-of-order arrival possible\n";
    std::cout << " Playback Artifacts     | Potential stalls/buffer waits   | Audible micro-clicks/gaps    \n";
    std::cout << " Delay Sensitivity      | Head-of-line blocking on delays | Low-latency, real-time pace  \n";
    std::cout << " Wireshark Filter       | tcp.port == 6000                | udp.port == 6001             \n";
    std::cout << " Loss Simulation Method | Transport / Network emulation   | Application-level frame drop \n";
    std::cout << " Typical Use Cases      | File transfers, HTTP, Podcasts  | VoIP, Gaming, Live Streaming \n";
    std::cout << "=========================================================================================\n\n";
}

int main() {
    printHeader();

    while (true) {
        printMenu();
        int choice = 0;
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string dummy;
            std::cin >> dummy;
            continue;
        }

        if (choice == 1) {
            runDemo("tcp_audio_server.exe",
                    "tcp_audio_client.exe audio/network_demo.wav 127.0.0.1 6000",
                    "TCP Audio Streaming (100% Reliable)");
        } else if (choice == 2) {
            runDemo("udp_audio_server.exe",
                    "udp_audio_client.exe audio/network_demo.wav 0.0 127.0.0.1 6001",
                    "UDP Audio Streaming (0% Loss Baseline)");
        } else if (choice == 3) {
            runDemo("udp_audio_server.exe",
                    "udp_audio_client.exe audio/network_demo.wav 0.05 127.0.0.1 6001",
                    "UDP Audio Streaming (5% Packet Loss)");
        } else if (choice == 4) {
            runDemo("udp_audio_server.exe",
                    "udp_audio_client.exe audio/network_demo.wav 0.10 127.0.0.1 6001",
                    "UDP Audio Streaming (10% Packet Loss)");
        } else if (choice == 5) {
            runDemo("udp_audio_server.exe",
                    "udp_audio_client.exe audio/network_demo.wav 0.20 127.0.0.1 6001",
                    "UDP Audio Streaming (20% Packet Loss)");
        } else if (choice == 6) {
            printComparisonTable();
        } else if (choice == 7) {
            std::cout << "\nExiting Real-Time Audio Demo. Goodbye!\n";
            break;
        } else {
            std::cout << "\nInvalid choice. Please select 1 through 7.\n";
        }
    }

    return 0;
}
