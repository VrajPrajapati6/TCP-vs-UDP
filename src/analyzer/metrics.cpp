/*
 * metrics.cpp - Performance calculation and reporting for NetPulse
 *
 * Implements throughput, packet loss, latency, jitter calculations,
 * formatted console output, CSV export, and result construction.
 *
 * This module is compiled into the main.exe so the orchestrator can
 * display and save results. It can also be compiled into server/client
 * executables if they need local calculations.
 */

#include "metrics.h"

// ============================================================
// Throughput: Total Data / Time
// ============================================================

double calculateThroughputMBps(int64_t totalBytes, double timeSec) {
    if (timeSec <= 0.0) return 0.0;
    return (static_cast<double>(totalBytes) / (1024.0 * 1024.0)) / timeSec;
}

double calculateThroughputMbps(int64_t totalBytes, double timeSec) {
    if (timeSec <= 0.0) return 0.0;
    return (static_cast<double>(totalBytes) * 8.0 / 1000000.0) / timeSec;
}

// ============================================================
// Packet Loss: (Sent - Received) / Sent * 100
// ============================================================

double calculatePacketLoss(int sent, int received) {
    if (sent <= 0) return 0.0;
    int lost = sent - received;
    if (lost < 0) lost = 0;
    return (static_cast<double>(lost) / static_cast<double>(sent)) * 100.0;
}

// ============================================================
// Average Latency
// ============================================================

double calculateAverageLatency(const std::vector<double>& latencies) {
    if (latencies.empty()) return 0.0;
    double sum = 0.0;
    for (double l : latencies) sum += l;
    return sum / static_cast<double>(latencies.size());
}

// ============================================================
// Jitter: mean of |D(i) - D(i-1)| for consecutive samples
// ============================================================

double calculateJitter(const std::vector<double>& latencies) {
    if (latencies.size() < 2) return 0.0;
    double sum = 0.0;
    for (size_t i = 1; i < latencies.size(); i++) {
        sum += std::fabs(latencies[i] - latencies[i - 1]);
    }
    return sum / static_cast<double>(latencies.size() - 1);
}

// ============================================================
// Display a single experiment result
// ============================================================

void displayResult(const ExperimentResult& result) {
    std::string protoStr = (result.protocol == Protocol::TCP) ? "TCP" : "UDP";
    std::string modeStr  = (result.mode == TransferMode::SYNTHETIC) ? "Synthetic Data" : "File Transfer";

    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "              EXPERIMENT RESULT (" << protoStr << ")\n";
    std::cout << "==========================================================\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  Protocol          : " << protoStr << "\n";
    std::cout << "  Mode              : " << modeStr << "\n";
    if (!result.filePath.empty()) {
        std::cout << "  File              : " << result.filePath << "\n";
    }
    std::cout << "  Data Size         : " << result.totalBytes << " bytes ("
              << std::setprecision(2)
              << (static_cast<double>(result.totalBytes) / (1024.0 * 1024.0)) << " MB)\n";
    std::cout << "  Chunk/Packet Size : " << result.chunkSize << " bytes\n";
    std::cout << "  Packets Sent      : " << result.packetsSent << "\n";
    std::cout << "  Packets Received  : " << result.packetsReceived << "\n";
    std::cout << "  Packets Lost      : " << result.packetsLost << "\n";
    std::cout << std::setprecision(2);
    std::cout << "  Packet Loss       : " << result.packetLossPercent << " %\n";
    std::cout << std::setprecision(4);
    std::cout << "  Transmission Time : " << result.transmissionTimeSec << " sec\n";
    std::cout << std::setprecision(2);
    std::cout << "  Throughput        : " << result.throughputMBps << " MB/s ("
              << result.throughputMbps << " Mbps)\n";
    std::cout << std::setprecision(4);
    std::cout << "  Average Latency   : " << result.averageLatencyMs << " ms\n";
    std::cout << "  Jitter            : " << result.jitterMs << " ms\n";
    if (result.protocol == Protocol::UDP) {
        std::cout << "  Out-of-Order Pkts : " << result.outOfOrderPackets << "\n";
    }
    if (result.mode == TransferMode::FILEXFER) {
        std::cout << "  File Integrity    : "
                  << (result.fileIntegrityPass ? "PASS" : "FAIL") << "\n";
    }
    std::cout << "  Timestamp         : " << result.timestamp << "\n";
    std::cout << "==========================================================\n\n";
}

// ============================================================
// Display TCP vs UDP side-by-side comparison
// ============================================================

void displayComparison(const ExperimentResult& tcp, const ExperimentResult& udp) {
    std::cout << "\n";
    std::cout << "================================================================\n";
    std::cout << "                  TCP vs UDP COMPARISON\n";
    std::cout << "================================================================\n";
    std::cout << std::left << std::setw(24) << "  Metric"
              << std::right << std::setw(16) << "TCP"
              << std::setw(16) << "UDP" << "\n";
    std::cout << "----------------------------------------------------------------\n";

    auto row = [](const std::string& label, const std::string& t, const std::string& u) {
        std::cout << "  " << std::left << std::setw(22) << label
                  << std::right << std::setw(16) << t
                  << std::setw(16) << u << "\n";
    };

    auto fmtBytes = [](int64_t b) -> std::string {
        std::ostringstream o;
        o << std::fixed << std::setprecision(2) << (static_cast<double>(b)/(1024.0*1024.0)) << " MB";
        return o.str();
    };

    auto fmtDbl = [](double v, int prec = 4) -> std::string {
        std::ostringstream o;
        o << std::fixed << std::setprecision(prec) << v;
        return o.str();
    };

    row("Data Size",       fmtBytes(tcp.totalBytes),       fmtBytes(udp.totalBytes));
    row("Chunk Size",      std::to_string(tcp.chunkSize),  std::to_string(udp.chunkSize));
    row("Packets Sent",    std::to_string(tcp.packetsSent),std::to_string(udp.packetsSent));
    row("Packets Received",std::to_string(tcp.packetsReceived), std::to_string(udp.packetsReceived));
    row("Packets Lost",    std::to_string(tcp.packetsLost),std::to_string(udp.packetsLost));
    row("Packet Loss %",   fmtDbl(tcp.packetLossPercent,2)+"%", fmtDbl(udp.packetLossPercent,2)+"%");
    row("Time (sec)",      fmtDbl(tcp.transmissionTimeSec), fmtDbl(udp.transmissionTimeSec));
    row("Throughput MB/s",  fmtDbl(tcp.throughputMBps,2),   fmtDbl(udp.throughputMBps,2));
    row("Throughput Mbps",  fmtDbl(tcp.throughputMbps,2),   fmtDbl(udp.throughputMbps,2));
    row("Avg Latency (ms)", fmtDbl(tcp.averageLatencyMs),   fmtDbl(udp.averageLatencyMs));
    row("Jitter (ms)",      fmtDbl(tcp.jitterMs),           fmtDbl(udp.jitterMs));
    row("Out-of-Order",     std::to_string(tcp.outOfOrderPackets), std::to_string(udp.outOfOrderPackets));

    if (tcp.mode == TransferMode::FILEXFER || udp.mode == TransferMode::FILEXFER) {
        row("File Integrity",
            tcp.mode == TransferMode::FILEXFER ? (tcp.fileIntegrityPass ? "PASS" : "FAIL") : "N/A",
            udp.mode == TransferMode::FILEXFER ? (udp.fileIntegrityPass ? "PASS" : "FAIL") : "N/A");
    }

    std::cout << "================================================================\n\n";
}

// ============================================================
// Save result to CSV
// ============================================================

void saveResultCSV(const std::string& csvPath, const ExperimentResult& result) {
    bool fileExists = false;
    {
        std::ifstream test(csvPath);
        fileExists = test.good();
    }

    std::ofstream out(csvPath, std::ios::app);
    if (!out.is_open()) {
        std::cerr << "[WARN] Cannot open CSV file: " << csvPath << std::endl;
        return;
    }

    // Write header if new file
    if (!fileExists) {
        out << "timestamp,protocol,mode,data_size_bytes,chunk_size,packets_sent,"
            << "packets_received,packets_lost,packet_loss_pct,transmission_time_sec,"
            << "throughput_MBps,throughput_Mbps,avg_latency_ms,jitter_ms,"
            << "out_of_order,file_integrity\n";
    }

    std::string protoStr = (result.protocol == Protocol::TCP) ? "TCP" : "UDP";
    std::string modeStr  = (result.mode == TransferMode::SYNTHETIC) ? "SYNTHETIC" : "FILE";
    std::string integrity = "N/A";
    if (result.mode == TransferMode::FILEXFER) {
        integrity = result.fileIntegrityPass ? "PASS" : "FAIL";
    }

    out << std::fixed << std::setprecision(6);
    out << result.timestamp << ","
        << protoStr << ","
        << modeStr << ","
        << result.totalBytes << ","
        << result.chunkSize << ","
        << result.packetsSent << ","
        << result.packetsReceived << ","
        << result.packetsLost << ","
        << std::setprecision(2) << result.packetLossPercent << ","
        << std::setprecision(6) << result.transmissionTimeSec << ","
        << std::setprecision(4) << result.throughputMBps << ","
        << result.throughputMbps << ","
        << result.averageLatencyMs << ","
        << result.jitterMs << ","
        << result.outOfOrderPackets << ","
        << integrity << "\n";

    std::cout << "  [INFO] Result saved to " << csvPath << "\n";
}

// ============================================================
// Build ExperimentResult from server key-value file + client data
// ============================================================

ExperimentResult buildResultFromKV(
    const std::vector<std::pair<std::string, std::string>>& serverKV,
    Protocol proto,
    TransferMode mode,
    int64_t totalBytes,
    int chunkSize,
    int packetsSentByClient,
    double clientTransmissionTimeSec,
    const std::string& originalFilePath)
{
    ExperimentResult r;
    r.protocol           = proto;
    r.mode               = mode;
    r.totalBytes         = totalBytes;
    r.chunkSize          = chunkSize;
    r.packetsSent        = packetsSentByClient;
    r.filePath           = originalFilePath;
    r.timestamp          = getTimestampString();

    // Read server-reported values
    r.packetsReceived    = std::atoi(kvLookup(serverKV, "packets_received", "0").c_str());
    r.outOfOrderPackets  = std::atoi(kvLookup(serverKV, "out_of_order", "0").c_str());

    std::string avgLat   = kvLookup(serverKV, "avg_latency_ms", "0.0");
    r.averageLatencyMs   = std::atof(avgLat.c_str());

    std::string jit      = kvLookup(serverKV, "jitter_ms", "0.0");
    r.jitterMs           = std::atof(jit.c_str());

    // Use client-measured transmission time (more accurate for throughput)
    r.transmissionTimeSec = clientTransmissionTimeSec;

    // Compute derived metrics
    r.packetsLost        = r.packetsSent - r.packetsReceived;
    if (r.packetsLost < 0) r.packetsLost = 0;
    r.packetLossPercent  = calculatePacketLoss(r.packetsSent, r.packetsReceived);
    r.throughputMBps     = calculateThroughputMBps(totalBytes, r.transmissionTimeSec);
    r.throughputMbps     = calculateThroughputMbps(totalBytes, r.transmissionTimeSec);

    // File integrity check
    r.fileIntegrityPass  = false;
    if (mode == TransferMode::FILEXFER && !originalFilePath.empty()) {
        std::string receivedPath = kvLookup(serverKV, "received_file", "");
        if (!receivedPath.empty()) {
            r.fileIntegrityPass = compareFiles(originalFilePath, receivedPath);
        }
    }

    return r;
}
