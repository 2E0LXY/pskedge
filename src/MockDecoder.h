#pragma once

#include "SignalTypes.h"

#include <QObject>
#include <QTimer>

class MockDecoder : public QObject {
    Q_OBJECT

public:
    explicit MockDecoder(QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void decoded(const DecodeLine &line);

private slots:
    void emitLine();

private:
    QTimer m_timer;
    int m_index = 0;
};
