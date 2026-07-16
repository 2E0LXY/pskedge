#pragma once

#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

class WaterfallWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaterfallWidget(QWidget *parent = nullptr);
    void setRxAudioHz(double audioHz);
    void setTxAudioHz(double audioHz);
    void setTxLockedToRx(bool locked);
    void addSpectrumLine(const QVector<double> &levels);

signals:
    void frequencyClicked(double audioHz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QColor colorForLevel(double value) const;
    int yForAudio(double audioHz) const;
    double audioForY(double y) const;
    QImage m_image;
    double m_rxAudioHz = 1420.0;
    double m_txAudioHz = 1420.0;
    bool m_txLockedToRx = true;
};
