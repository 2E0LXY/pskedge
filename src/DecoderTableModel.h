#pragma once

#include "SignalTypes.h"

#include <QAbstractTableModel>
#include <QVector>

class DecoderTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum class Mode {
        ActiveDecoders,
        SweeperCandidates
    };

    explicit DecoderTableModel(Mode mode, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addOrUpdate(const DecodeLine &line);
    void clear();
    DecodeLine lineAt(int row) const;
    QVector<DecodeLine> lines() const;

private:
    QString columnText(const DecodeLine &line, int column) const;

    Mode m_mode;
    QVector<DecodeLine> m_lines;
    int m_limit = 16;
};
