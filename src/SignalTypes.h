#pragma once

#include <QString>

struct SignalMetrics {
    double snrDb = 0.0;
    double signalLevelDb = -120.0;
    double noiseFloorDb = -120.0;
    double audioFrequencyHz = 1000.0;
    double rfFrequencyMhz = 14.071420;
    double bandwidthHz = 60.0;
    double driftHzPerMinute = 0.0;
    int qualityPercent = 0;
    QString lockQuality = "Searching";
    QString imdDb = "n/a";
};

struct DecodeLine {
    int channel = 1;
    QString callsign;
    QString mode = "BPSK31";
    QString state = "Searching";
    QString text;
    SignalMetrics metrics;
};
