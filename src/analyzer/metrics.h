#ifndef METRICS_H
#define METRICS_H

/*
 * metrics.h - Performance measurement declarations for NetPulse
 *
 * Defines the functions that calculate throughput, packet loss,
 * latency, jitter, and generate formatted result displays and CSV output.
 * Keeps measurement logic separate from networking code.
 */

#include "../../include/common.h"

// ============================================================
// Metric Calculation Functions
// ============================================================

// Calculate throughput given total bytes and time in seconds
double calculateThroughputMBps(int64_t totalBytes, double timeSec);
double calculateThroughputMbps(int64_t totalBytes, double timeSec);

// Calculate packet loss percentage
double calculatePacketLoss(int sent, int received);

// Calculate average latency from a vector of latency samples (in ms)
double calculateAverageLatency(const std::vector<double>& latencies);

/*
 * Calculate jitter as the average of absolute differences between
 * consecutive latency samples, following RFC 3550 approach:
 *
 *   J(i) = |D(i) - D(i-1)|
 *   Jitter = mean of all J(i)
 *
 * This is a simplified but academically clear method.
 */
double calculateJitter(const std::vector<double>& latencies);

// ============================================================
// Result Display and Export
// ============================================================

// Print a formatted single-experiment result to console
void displayResult(const ExperimentResult& result);

// Print a side-by-side TCP vs UDP comparison table
void displayComparison(const ExperimentResult& tcp, const ExperimentResult& udp);

// Append a result row to a CSV file
void saveResultCSV(const std::string& csvPath, const ExperimentResult& result);

// Build an ExperimentResult from a server result file + client-side data
ExperimentResult buildResultFromKV(
    const std::vector<std::pair<std::string, std::string>>& serverKV,
    Protocol proto,
    TransferMode mode,
    int64_t totalBytes,
    int chunkSize,
    int packetsSentByClient,
    double clientTransmissionTimeSec,
    const std::string& originalFilePath
);

#endif // METRICS_H
