/*
 * main.cpp - NetPulse Orchestrator & Console Interface
 *
 * This is the user-facing entry point for the project.
 *
 * Architecture:
 *   main.exe provides a clean menu. When the user selects an experiment,
 *   main.exe:
 *     1. Launches the appropriate server (tcp_server.exe or udp_server.exe)
 *        as a background process.
 *     2. Waits briefly for the server to initialize.
 *     3. Launches the client (tcp_client.exe or udp_client.exe) and captures
 *        its stdout to read the RESULT line.
 *     4. Waits for both processes to complete.
 *     5. Reads the server's result file from results/.
 *     6. Combines client + server data to build ExperimentResult.
 *     7. Displays formatted results and saves to CSV.
 *
 *   For the "TCP vs UDP Comparison" option, both experiments run sequentially
 *   and results are shown side-by-side.
 *
 * Compilation:
 *   g++ src/main.cpp src/analyzer/metrics.cpp -o main.exe -lws2_32 -std=c++17
 *
 * Prerequisites:
 *   tcp_server.exe, tcp_client.exe, udp_server.exe, udp_client.exe must be
 *   compiled and present in the project root directory.
 */

#include "../include/common.h"
#include "analyzer/metrics.h"
#include <windows.h>
#include <cstdio>

// ============================================================
// Helper: Run a command and capture its stdout
// ============================================================

static std::string runCommandCapture(const std::string& cmd) {
    std::string result;
    // Use _popen to capture stdout from child process
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Failed to run: " << cmd << "\n";
        return "";
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        result += buf;
    }
    pclose(pipe);
    return result;
}

// ============================================================
// Helper: Launch a background process (no window capture)
// ============================================================

static HANDLE launchBackground(const std::string& cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    std::memset(&pi, 0, sizeof(pi));

    // Need mutable copy of command string for CreateProcessA
    char cmdBuf[2048];
    strncpy(cmdBuf, cmd.c_str(), sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = '\0';

    if (!CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        std::cerr << "[ERROR] Failed to launch: " << cmd
                  << " (Error " << GetLastError() << ")\n";
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// ============================================================
// Helper: Wait for process to exit
// ============================================================

static void waitForProcess(HANDLE hProcess, DWORD timeoutMs = 60000) {
    if (hProcess) {
        WaitForSingleObject(hProcess, timeoutMs);
        CloseHandle(hProcess);
    }
}

// ============================================================
// Helper: Delete a file if it exists (clean previous results)
// ============================================================

static void deleteFileIfExists(const std::string& path) {
    std::remove(path.c_str());
}

// ============================================================
// Helper: Wait for a result file to appear
// ============================================================

static bool waitForResultFile(const std::string& path, int maxWaitMs = 15000) {
    int waited = 0;
    while (waited < maxWaitMs) {
        std::ifstream f(path);
        if (f.good()) {
            f.close();
            return true;
        }
        Sleep(250);
        waited += 250;
    }
    return false;
}

// ============================================================
// Helper: Parse RESULT line from client stdout
// ============================================================

struct ClientResult {
    int packetsSent;
    int totalPackets;   // For UDP: total including skipped
    double transmissionTimeSec;
    int64_t totalBytes;
    bool valid;
};

static ClientResult parseClientResult(const std::string& output) {
    ClientResult cr;
    cr.valid = false;
    cr.packetsSent = 0;
    cr.totalPackets = 0;
    cr.transmissionTimeSec = 0.0;
    cr.totalBytes = 0;

    // Find line starting with "RESULT|"
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.substr(0, 7) == "RESULT|") {
            // TCP:  RESULT|packets_sent|time|bytes
            // UDP:  RESULT|packets_sent|total_packets|time|bytes
            std::string payload = line.substr(7);
            std::istringstream ps(payload);
            std::string tok;
            std::vector<std::string> tokens;
            while (std::getline(ps, tok, '|')) {
                tokens.push_back(tok);
            }
            if (tokens.size() == 3) {
                // TCP format
                cr.packetsSent = std::atoi(tokens[0].c_str());
                cr.totalPackets = cr.packetsSent;
                cr.transmissionTimeSec = std::atof(tokens[1].c_str());
                cr.totalBytes = std::atoll(tokens[2].c_str());
                cr.valid = true;
            } else if (tokens.size() >= 4) {
                // UDP format
                cr.packetsSent = std::atoi(tokens[0].c_str());
                cr.totalPackets = std::atoi(tokens[1].c_str());
                cr.transmissionTimeSec = std::atof(tokens[2].c_str());
                cr.totalBytes = std::atoll(tokens[3].c_str());
                cr.valid = true;
            }
            break;
        }
    }
    return cr;
}

// ============================================================
// Get user input helpers
// ============================================================

static int64_t getDataSizeMB() {
    double mb;
    std::cout << "  Enter data size in MB (e.g. 1, 5, 10): ";
    std::cin >> mb;
    if (mb <= 0 || mb > 1000) {
        std::cout << "  Invalid size. Using 1 MB.\n";
        mb = 1.0;
    }
    return static_cast<int64_t>(mb * 1024 * 1024);
}

static int getChunkSize() {
    int cs;
    std::cout << "  Enter chunk/packet size in bytes (64 - 60000): ";
    std::cin >> cs;
    if (cs < MIN_CHUNK_SIZE) cs = MIN_CHUNK_SIZE;
    if (cs > MAX_UDP_PAYLOAD) cs = MAX_UDP_PAYLOAD;
    return cs;
}

static std::string getFilePath() {
    std::string path;
    std::cout << "  Enter file path (e.g. test_files/sample.txt): ";
    std::cin >> path;
    std::ifstream test(path);
    if (!test.good()) {
        std::cerr << "  [WARN] File not found: " << path << "\n";
    }
    return path;
}

static double getLossRate() {
    double lr;
    std::cout << "  Enter simulated packet loss rate (0.0 - 1.0, e.g. 0.1 for 10%): ";
    std::cin >> lr;
    if (lr < 0.0) lr = 0.0;
    if (lr > 1.0) lr = 1.0;
    return lr;
}

static int getDelayMs() {
    int d;
    std::cout << "  Enter artificial per-packet delay in ms (0 for none): ";
    std::cin >> d;
    if (d < 0) d = 0;
    return d;
}

// ============================================================
// Run a TCP experiment
// ============================================================

static ExperimentResult runTcpExperiment(TransferMode mode) {
    std::cout << "\n  ---- TCP Experiment Configuration ----\n";

    int64_t dataSize = 0;
    int chunkSize = DEFAULT_CHUNK_SIZE;
    std::string filePath;

    if (mode == TransferMode::SYNTHETIC) {
        dataSize = getDataSizeMB();
        chunkSize = getChunkSize();
    } else {
        filePath = getFilePath();
        chunkSize = getChunkSize();
        // dataSize will be auto-detected by client from file
        dataSize = 0;
    }

    std::cout << "\n  Starting TCP experiment...\n";

    // Clean previous result
    deleteFileIfExists(TCP_SERVER_RESULT_FILE);

    // Launch TCP server in background
    HANDLE hServer = launchBackground("tcp_server.exe");
    if (!hServer) {
        std::cerr << "  [ERROR] Could not start tcp_server.exe\n";
        ExperimentResult empty;
        std::memset(&empty, 0, sizeof(empty));
        return empty;
    }
    Sleep(500); // Let server bind and listen

    // Build client command
    std::ostringstream clientCmd;
    clientCmd << "tcp_client.exe "
              << (mode == TransferMode::FILEXFER ? "FILE" : "SYNTHETIC")
              << " " << dataSize
              << " " << chunkSize;
    if (mode == TransferMode::FILEXFER) {
        clientCmd << " " << filePath;
    }

    // Run client and capture stdout
    std::string clientOutput = runCommandCapture(clientCmd.str());
    ClientResult cr = parseClientResult(clientOutput);

    // Wait for server to finish
    waitForProcess(hServer, 30000);

    if (!cr.valid) {
        std::cerr << "  [ERROR] Could not parse client result.\n";
        ExperimentResult empty;
        std::memset(&empty, 0, sizeof(empty));
        return empty;
    }

    // Wait for server result file
    if (!waitForResultFile(TCP_SERVER_RESULT_FILE)) {
        std::cerr << "  [WARN] Server result file not found. Using client-only data.\n";
    }

    // Read server results
    std::vector<std::pair<std::string, std::string>> serverKV;
    readResultFile(TCP_SERVER_RESULT_FILE, serverKV);

    // Build combined result
    ExperimentResult result = buildResultFromKV(
        serverKV, Protocol::TCP, mode,
        cr.totalBytes, chunkSize, cr.packetsSent,
        cr.transmissionTimeSec, filePath
    );

    return result;
}

// ============================================================
// Run a UDP experiment
// ============================================================

static ExperimentResult runUdpExperiment(TransferMode mode) {
    std::cout << "\n  ---- UDP Experiment Configuration ----\n";

    int64_t dataSize = 0;
    int chunkSize = DEFAULT_CHUNK_SIZE;
    std::string filePath;
    double lossRate = 0.0;
    int delayMs = 0;

    if (mode == TransferMode::SYNTHETIC) {
        dataSize = getDataSizeMB();
        chunkSize = getChunkSize();
    } else {
        filePath = getFilePath();
        chunkSize = getChunkSize();
        dataSize = 0;
    }

    lossRate = getLossRate();
    delayMs = getDelayMs();

    std::cout << "\n  Starting UDP experiment...\n";

    // Clean previous result
    deleteFileIfExists(UDP_SERVER_RESULT_FILE);

    // Launch UDP server in background
    HANDLE hServer = launchBackground("udp_server.exe");
    if (!hServer) {
        std::cerr << "  [ERROR] Could not start udp_server.exe\n";
        ExperimentResult empty;
        std::memset(&empty, 0, sizeof(empty));
        return empty;
    }
    Sleep(500); // Let server bind

    // Build client command
    std::ostringstream clientCmd;
    clientCmd << "udp_client.exe "
              << (mode == TransferMode::FILEXFER ? "FILE" : "SYNTHETIC")
              << " " << dataSize
              << " " << chunkSize
              << " " << std::fixed << std::setprecision(2) << lossRate
              << " " << delayMs;
    if (mode == TransferMode::FILEXFER) {
        clientCmd << " " << filePath;
    }

    // Run client and capture stdout
    std::string clientOutput = runCommandCapture(clientCmd.str());
    ClientResult cr = parseClientResult(clientOutput);

    // Wait for server to finish
    waitForProcess(hServer, 30000);

    if (!cr.valid) {
        std::cerr << "  [ERROR] Could not parse client result.\n";
        ExperimentResult empty;
        std::memset(&empty, 0, sizeof(empty));
        return empty;
    }

    // Wait for server result file
    if (!waitForResultFile(UDP_SERVER_RESULT_FILE)) {
        std::cerr << "  [WARN] Server result file not found. Using client-only data.\n";
    }

    // Read server results
    std::vector<std::pair<std::string, std::string>> serverKV;
    readResultFile(UDP_SERVER_RESULT_FILE, serverKV);

    // Build combined result - for UDP, totalPackets is the full count (including skipped)
    ExperimentResult result = buildResultFromKV(
        serverKV, Protocol::UDP, mode,
        cr.totalBytes, chunkSize, cr.totalPackets,
        cr.transmissionTimeSec, filePath
    );

    return result;
}

// ============================================================
// Display Banner
// ============================================================

static void displayBanner() {
    std::cout << "\n";
    std::cout << "  ================================================================\n";
    std::cout << "                        N E T P U L S E\n";
    std::cout << "            TCP vs UDP Network Performance Analyzer\n";
    std::cout << "  ================================================================\n";
    std::cout << "  A Computer Networks experimental tool for comparing TCP and UDP\n";
    std::cout << "  behavior through real socket communication on Windows (Winsock2).\n";
    std::cout << "  ================================================================\n\n";
}

// ============================================================
// Display Menu
// ============================================================

static int displayMenu() {
    std::cout << "  --------------------------------------------------------\n";
    std::cout << "  1. TCP Performance Test      (Synthetic Data)\n";
    std::cout << "  2. UDP Performance Test       (Synthetic Data)\n";
    std::cout << "  3. TCP File Transfer\n";
    std::cout << "  4. UDP File Transfer\n";
    std::cout << "  5. TCP vs UDP Comparison      (Synthetic Data)\n";
    std::cout << "  6. Packet Size Experiment     (Multiple chunk sizes)\n";
    std::cout << "  7. Exit\n";
    std::cout << "  --------------------------------------------------------\n";
    std::cout << "  Enter choice: ";
    int choice;
    std::cin >> choice;
    return choice;
}

// ============================================================
// Packet Size Experiment (multiple chunk sizes)
// ============================================================

static void runPacketSizeExperiment() {
    std::cout << "\n  ---- Packet Size Experiment ----\n";
    std::cout << "  This experiment runs both TCP and UDP with multiple chunk sizes\n";
    std::cout << "  and compares throughput, latency, and loss.\n\n";

    int64_t dataSize = getDataSizeMB();

    int sizes[] = { 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    int numSizes = sizeof(sizes) / sizeof(sizes[0]);

    std::vector<ExperimentResult> tcpResults;
    std::vector<ExperimentResult> udpResults;

    for (int i = 0; i < numSizes; i++) {
        int cs = sizes[i];
        std::cout << "\n  === Chunk size: " << cs << " bytes ===\n";

        // TCP
        {
            std::cout << "  [TCP] Running...\n";
            deleteFileIfExists(TCP_SERVER_RESULT_FILE);
            HANDLE hServer = launchBackground("tcp_server.exe");
            Sleep(500);
            std::ostringstream cmd;
            cmd << "tcp_client.exe SYNTHETIC " << dataSize << " " << cs;
            std::string out = runCommandCapture(cmd.str());
            ClientResult cr = parseClientResult(out);
            waitForProcess(hServer, 30000);
            std::vector<std::pair<std::string, std::string>> skv;
            if (waitForResultFile(TCP_SERVER_RESULT_FILE))
                readResultFile(TCP_SERVER_RESULT_FILE, skv);
            if (cr.valid) {
                ExperimentResult r = buildResultFromKV(
                    skv, Protocol::TCP, TransferMode::SYNTHETIC,
                    cr.totalBytes, cs, cr.packetsSent, cr.transmissionTimeSec, "");
                tcpResults.push_back(r);
            }
        }

        // UDP (no simulated loss for comparison fairness)
        {
            std::cout << "  [UDP] Running...\n";
            deleteFileIfExists(UDP_SERVER_RESULT_FILE);
            HANDLE hServer = launchBackground("udp_server.exe");
            Sleep(500);
            std::ostringstream cmd;
            cmd << "udp_client.exe SYNTHETIC " << dataSize << " " << cs << " 0.0 0";
            std::string out = runCommandCapture(cmd.str());
            ClientResult cr = parseClientResult(out);
            waitForProcess(hServer, 30000);
            std::vector<std::pair<std::string, std::string>> skv;
            if (waitForResultFile(UDP_SERVER_RESULT_FILE))
                readResultFile(UDP_SERVER_RESULT_FILE, skv);
            if (cr.valid) {
                ExperimentResult r = buildResultFromKV(
                    skv, Protocol::UDP, TransferMode::SYNTHETIC,
                    cr.totalBytes, cs, cr.totalPackets, cr.transmissionTimeSec, "");
                udpResults.push_back(r);
            }
        }
    }

    // Display comparison table
    std::cout << "\n";
    std::cout << "  ====================================================================\n";
    std::cout << "                    PACKET SIZE EXPERIMENT RESULTS\n";
    std::cout << "  ====================================================================\n";
    std::cout << "  " << std::left
              << std::setw(10) << "ChunkSz"
              << std::setw(12) << "TCP MB/s"
              << std::setw(12) << "UDP MB/s"
              << std::setw(10) << "TCP Time"
              << std::setw(10) << "UDP Time"
              << std::setw(10) << "UDP Loss"
              << "\n";
    std::cout << "  --------------------------------------------------------------------\n";

    size_t count = std::min(tcpResults.size(), udpResults.size());
    for (size_t i = 0; i < count; i++) {
        std::cout << "  " << std::left << std::fixed
                  << std::setw(10) << tcpResults[i].chunkSize
                  << std::setw(12) << std::setprecision(2) << tcpResults[i].throughputMBps
                  << std::setw(12) << std::setprecision(2) << udpResults[i].throughputMBps
                  << std::setw(10) << std::setprecision(3) << tcpResults[i].transmissionTimeSec
                  << std::setw(10) << std::setprecision(3) << udpResults[i].transmissionTimeSec
                  << std::setw(10) << std::setprecision(2) << udpResults[i].packetLossPercent << "%"
                  << "\n";
    }
    std::cout << "  ====================================================================\n";

    // Save all to CSV
    for (auto& r : tcpResults) saveResultCSV("results/tcp_results.csv", r);
    for (auto& r : udpResults) saveResultCSV("results/udp_results.csv", r);
}

// ============================================================
// Main
// ============================================================

int main() {
    // Ensure results directory exists
    CreateDirectoryA("results", NULL);
    CreateDirectoryA("test_files", NULL);
    CreateDirectoryA("wireshark", NULL);

    displayBanner();

    bool running = true;
    while (running) {
        int choice = displayMenu();

        switch (choice) {
        case 1: {
            // TCP Performance Test (Synthetic)
            ExperimentResult r = runTcpExperiment(TransferMode::SYNTHETIC);
            if (r.totalBytes > 0) {
                displayResult(r);
                saveResultCSV("results/tcp_results.csv", r);
            }
            break;
        }
        case 2: {
            // UDP Performance Test (Synthetic)
            ExperimentResult r = runUdpExperiment(TransferMode::SYNTHETIC);
            if (r.totalBytes > 0) {
                displayResult(r);
                saveResultCSV("results/udp_results.csv", r);
            }
            break;
        }
        case 3: {
            // TCP File Transfer
            ExperimentResult r = runTcpExperiment(TransferMode::FILEXFER);
            if (r.totalBytes > 0) {
                displayResult(r);
                saveResultCSV("results/tcp_results.csv", r);
            }
            break;
        }
        case 4: {
            // UDP File Transfer
            ExperimentResult r = runUdpExperiment(TransferMode::FILEXFER);
            if (r.totalBytes > 0) {
                displayResult(r);
                saveResultCSV("results/udp_results.csv", r);
            }
            break;
        }
        case 5: {
            // TCP vs UDP Comparison
            std::cout << "\n  === TCP vs UDP Comparison ===\n";
            std::cout << "  Both experiments will use the same parameters.\n\n";

            int64_t dataSize = getDataSizeMB();
            int chunkSize = getChunkSize();
            double lossRate = getLossRate();
            int delayMs = getDelayMs();

            // Run TCP
            std::cout << "\n  --- Running TCP experiment ---\n";
            deleteFileIfExists(TCP_SERVER_RESULT_FILE);
            HANDLE hTcpServer = launchBackground("tcp_server.exe");
            Sleep(500);
            {
                std::ostringstream cmd;
                cmd << "tcp_client.exe SYNTHETIC " << dataSize << " " << chunkSize;
                std::string out = runCommandCapture(cmd.str());
                ClientResult cr = parseClientResult(out);
                waitForProcess(hTcpServer, 30000);

                std::vector<std::pair<std::string, std::string>> skv;
                if (waitForResultFile(TCP_SERVER_RESULT_FILE))
                    readResultFile(TCP_SERVER_RESULT_FILE, skv);

                ExperimentResult tcpResult;
                if (cr.valid) {
                    tcpResult = buildResultFromKV(
                        skv, Protocol::TCP, TransferMode::SYNTHETIC,
                        cr.totalBytes, chunkSize, cr.packetsSent,
                        cr.transmissionTimeSec, "");
                }

                // Run UDP
                std::cout << "\n  --- Running UDP experiment ---\n";
                deleteFileIfExists(UDP_SERVER_RESULT_FILE);
                HANDLE hUdpServer = launchBackground("udp_server.exe");
                Sleep(500);
                {
                    std::ostringstream ucmd;
                    ucmd << "udp_client.exe SYNTHETIC " << dataSize << " " << chunkSize
                         << " " << std::fixed << std::setprecision(2) << lossRate
                         << " " << delayMs;
                    std::string uout = runCommandCapture(ucmd.str());
                    ClientResult ucr = parseClientResult(uout);
                    waitForProcess(hUdpServer, 30000);

                    std::vector<std::pair<std::string, std::string>> uskv;
                    if (waitForResultFile(UDP_SERVER_RESULT_FILE))
                        readResultFile(UDP_SERVER_RESULT_FILE, uskv);

                    ExperimentResult udpResult;
                    if (ucr.valid) {
                        udpResult = buildResultFromKV(
                            uskv, Protocol::UDP, TransferMode::SYNTHETIC,
                            ucr.totalBytes, chunkSize, ucr.totalPackets,
                            ucr.transmissionTimeSec, "");
                    }

                    // Display comparison
                    if (cr.valid && ucr.valid) {
                        displayComparison(tcpResult, udpResult);
                        saveResultCSV("results/tcp_results.csv", tcpResult);
                        saveResultCSV("results/udp_results.csv", udpResult);

                        // Also save comparison CSV
                        std::ofstream comp("results/comparison.csv");
                        if (comp.is_open()) {
                            comp << "metric,TCP,UDP\n";
                            comp << "data_size_MB," << std::fixed << std::setprecision(2)
                                 << (double)tcpResult.totalBytes/(1024*1024) << ","
                                 << (double)udpResult.totalBytes/(1024*1024) << "\n";
                            comp << "chunk_size," << tcpResult.chunkSize << ","
                                 << udpResult.chunkSize << "\n";
                            comp << "packets_sent," << tcpResult.packetsSent << ","
                                 << udpResult.packetsSent << "\n";
                            comp << "packets_received," << tcpResult.packetsReceived << ","
                                 << udpResult.packetsReceived << "\n";
                            comp << "packet_loss_pct," << tcpResult.packetLossPercent << ","
                                 << udpResult.packetLossPercent << "\n";
                            comp << "time_sec," << std::setprecision(6)
                                 << tcpResult.transmissionTimeSec << ","
                                 << udpResult.transmissionTimeSec << "\n";
                            comp << "throughput_MBps," << std::setprecision(4)
                                 << tcpResult.throughputMBps << ","
                                 << udpResult.throughputMBps << "\n";
                            comp << "throughput_Mbps," << tcpResult.throughputMbps << ","
                                 << udpResult.throughputMbps << "\n";
                            comp << "avg_latency_ms," << tcpResult.averageLatencyMs << ","
                                 << udpResult.averageLatencyMs << "\n";
                            comp << "jitter_ms," << tcpResult.jitterMs << ","
                                 << udpResult.jitterMs << "\n";
                            comp.close();
                            std::cout << "  [INFO] Comparison saved to results/comparison.csv\n";
                        }
                    }
                }
            }
            break;
        }
        case 6: {
            // Packet Size Experiment
            runPacketSizeExperiment();
            break;
        }
        case 7:
            running = false;
            std::cout << "\n  Goodbye!\n\n";
            break;
        default:
            std::cout << "  Invalid choice. Please try again.\n";
        }
    }

    return 0;
}
