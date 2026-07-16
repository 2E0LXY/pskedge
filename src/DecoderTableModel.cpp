#include "DecoderTableModel.h"

#include <QBrush>
#include <QColor>
#include <QStringList>

DecoderTableModel::DecoderTableModel(Mode mode, QObject *parent)
    : QAbstractTableModel(parent), m_mode(mode)
{
    m_limit = (mode == Mode::ActiveDecoders) ? 16 : 80;
}

int DecoderTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_mode == Mode::ActiveDecoders ? 16 : m_lines.size();
}

int DecoderTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 8;
}

QVariant DecoderTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    if (index.row() >= m_lines.size()) {
        if (role == Qt::DisplayRole && m_mode == Mode::ActiveDecoders && index.column() == 0) {
            return QString::number(index.row() + 1);
        }
        if (role == Qt::ForegroundRole) {
            return QBrush(QColor(80, 105, 116));
        }
        return {};
    }

    const DecodeLine &line = m_lines.at(index.row());
    if (role == Qt::DisplayRole) {
        return columnText(line, index.column());
    }
    if (role == Qt::ForegroundRole) {
        if (line.metrics.qualityPercent < 75) {
            return QBrush(QColor(255, 196, 87));
        }
        if (line.text.contains("CQ", Qt::CaseInsensitive)) {
            return QBrush(QColor(111, 233, 255));
        }
        return QBrush(QColor(216, 247, 255));
    }
    return {};
}

QVariant DecoderTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    static const QStringList headers = {"Ch", "State", "Call", "Audio", "Mode", "SNR", "Q", "Text"};
    return headers.value(section);
}

void DecoderTableModel::addOrUpdate(const DecodeLine &line)
{
    if (m_mode == Mode::ActiveDecoders) {
        for (int i = 0; i < m_lines.size(); ++i) {
            if (m_lines.at(i).channel == line.channel) {
                m_lines[i] = line;
                emit dataChanged(index(i, 0), index(i, columnCount() - 1));
                return;
            }
        }
        if (m_lines.size() < m_limit) {
            const int row = m_lines.size();
            m_lines.append(line);
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        }
        return;
    }

    if (m_lines.size() >= m_limit) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_lines.removeFirst();
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), m_lines.size(), m_lines.size());
    m_lines.append(line);
    endInsertRows();
}

DecodeLine DecoderTableModel::lineAt(int row) const
{
    if (row < 0 || row >= m_lines.size()) {
        return {};
    }
    return m_lines.at(row);
}

QVector<DecodeLine> DecoderTableModel::lines() const
{
    return m_lines;
}

void DecoderTableModel::clear()
{
    if (m_lines.isEmpty()) {
        return;
    }
    beginRemoveRows(QModelIndex(), 0, m_lines.size() - 1);
    m_lines.clear();
    endRemoveRows();
}

QString DecoderTableModel::columnText(const DecodeLine &line, int column) const
{
    switch (column) {
    case 0: return QString::number(line.channel);
    case 1: return line.metrics.lockQuality;
    case 2: return line.callsign;
    case 3: return QString::number(line.metrics.audioFrequencyHz, 'f', 0);
    case 4: return line.mode;
    case 5: return QString::number(line.metrics.snrDb, 'f', 1);
    case 6: return QString::number(line.metrics.qualityPercent) + "%";
    case 7: return line.text;
    default: return {};
    }
}
