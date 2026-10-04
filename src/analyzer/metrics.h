#ifndef METRICS_H
#define METRICS_H

#include "../../include/common.h"

double calculateThroughputMBps(int64_t totalBytes, double timeSec);
double calculateThroughputMbps(int64_t totalBytes, double timeSec);

double calculatePacketLoss(int sent, int received);

double calculateAverageLatency(const std::vector<double>& latencies);

double calculateJitter(const std::vector<double>& latencies);

void displayResult(const ExperimentResult& result);

void displayComparison(const ExperimentResult& tcp, const ExperimentResult& udp);

void saveResultCSV(const std::string& csvPath, const ExperimentResult& result);

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
