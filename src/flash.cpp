/*
 * flash.cpp
 *
 *  Created on: 2019-08-18 19:00
 *      Author: Jack Chen <redchenjs@live.com>
 */

#include <cstdio>
#include <string>
#include <iostream>

#include <QtCore>
#include <QtBluetooth>

#include "flash.h"

#define CMD_FMT_ERASE_ALL   "MTD+ERASE:ALL!"
#define CMD_FMT_ERASE       "MTD+ERASE:0x%x+0x%x"
#define CMD_FMT_WRITE       "MTD+WRITE:0x%x+0x%x"
#define CMD_FMT_READ        "MTD+READ:0x%x+0x%x"
#define CMD_FMT_INFO        "MTD+INFO?"

enum cmd_idx {
    CMD_IDX_ERASE_ALL = 0x0,
    CMD_IDX_ERASE     = 0x1,
    CMD_IDX_WRITE     = 0x2,
    CMD_IDX_READ      = 0x3,
    CMD_IDX_INFO      = 0x4,
};

enum rsp_idx {
    RSP_IDX_NONE  = 0x0,
    RSP_IDX_TRUE  = 0x1,
    RSP_IDX_FALSE = 0x2,
};

typedef struct {
    const bool flag;
    const char fmt[32];
} rsp_fmt_t;

static const rsp_fmt_t rsp_fmt[] = {
    {  true, "OK\r\n" },     // OK
    {  true, "DONE\r\n" },   // Done
    { false, "FAIL\r\n" },   // Fail
    { false, "ERROR\r\n" },  // Error
};

void FlashProgrammer::readyRead(void)
{
    QByteArray recv = m_device->readAll();

    char *recv_buff = recv.data();
    uint32_t recv_size = static_cast<uint32_t>(recv.size());

    for (uint8_t i=0; i<sizeof(rsp_fmt)/sizeof(rsp_fmt_t); i++) {
        if (strncmp(recv_buff, rsp_fmt[i].fmt, strlen(rsp_fmt[i].fmt)) == 0) {
            if (rsp_fmt[i].flag == true) {
                switch (m_cmd_idx) {
                case CMD_IDX_ERASE_ALL:
                case CMD_IDX_ERASE:
                    emit finished(OK);
                    break;
                case CMD_IDX_WRITE:
                    if (rw_in_progress == RW_NONE) {
                        rw_in_progress = RW_WRITE;
                        QTimer::singleShot(0, this, [&]()->void{this->sendData();});
                    } else {
                        std::cout << std::endl;
                        rw_in_progress = RW_NONE;
                        emit finished(OK);
                    }
                    break;
                case CMD_IDX_READ:
                    rw_in_progress = RW_READ;
                    break;
                default:
                    break;
                }
            } else {
                if (rw_in_progress != RW_NONE) {
                    std::cout << std::endl;
                }
                emit finished(ERR_REMOTE);
            }
            if (recv_size == strlen(rsp_fmt[i].fmt)) {
                std::cout << "<= " << recv_buff;
                return;
            }
        }
    }

    if (rw_in_progress == RW_READ) {
        if ((data_size - data_recv) <= recv_size) {
            data_fd->write(recv_buff, data_size - data_recv);
            data_fd->close();
            data_recv += data_size - data_recv;
            std::cout << "<< RECV:100%" << std::endl;
            std::cout << "== DONE" << std::endl;
            rw_in_progress = RW_NONE;
            emit finished(OK);
        } else {
            data_fd->write(recv_buff, recv_size);
            data_recv += recv_size;
            std::cout << "<< RECV:" << data_recv*100/data_size << "%\r";
        }
    } else {
        std::cout << "<= " << recv_buff;
        emit finished(OK);
    }
}

void FlashProgrammer::bytesWritten(void)
{
    uint32_t data_sent = data_size - static_cast<uint32_t>(m_device->bytesToWrite());

    if (data_sent == data_size) {
        std::cout << ">> SENT:100%\r";
        disconnect(m_device, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten()));
    } else {
        std::cout << ">> SENT:" << data_sent*100/data_size << "%\r";
    }
}

void FlashProgrammer::sendData(void)
{
    char read_buff[4096] = {0};

    uint32_t pkt = 0;
    for (pkt=0; pkt<data_size/sizeof(read_buff); pkt++) {
        if (data_fd->read(read_buff, sizeof(read_buff)) != sizeof(read_buff)) {
            std::cout << std::endl << "== ERROR" << std::endl;
            data_fd->close();
            emit finished(ERR_FILE);
            return;
        }

        m_device->write(read_buff, sizeof(read_buff));

        std::cout << "== READ:" << pkt*sizeof(read_buff)*100/data_size << "%\r";
    }

    uint32_t data_remain = data_size - pkt * sizeof(read_buff);
    if (data_remain != 0 && data_remain < sizeof(read_buff)) {
        if (data_fd->read(read_buff, data_remain) != data_remain) {
            std::cout << std::endl << "== ERROR" << std::endl;
            data_fd->close();
            emit finished(ERR_FILE);
            return;
        }

        m_device->write(read_buff, data_remain);

        std::cout << "== READ:" << (pkt*sizeof(read_buff)+data_remain)*100/data_size << "%\r";
    }

    std::cout << "== READ:100%" << std::endl;

    connect(m_device, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten()));

    data_fd->close();
}

void FlashProgrammer::printUsage(void)
{
    std::cout << "Usage:" << std::endl;
    std::cout << "    " << m_arg[0] << " BD_ADDR COMMAND\n" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "    erase_all\t\t\terase full flash chip" << std::endl;
    std::cout << "    erase addr length\t\terase flash start from [addr] for [length] bytes" << std::endl;
    std::cout << "    write addr length data.bin\twrite [data.bin] to flash from [addr] for [length] bytes" << std::endl;
    std::cout << "    read  addr length data.bin\tread flash from [addr] for [length] bytes to [data.bin]" << std::endl;
    std::cout << "    info\t\t\tread flash info" << std::endl;

    emit finished();
}

void FlashProgrammer::connected(void)
{
    std::cout << "=> " << m_cmd_str;

    m_device->write(m_cmd_str, static_cast<uint32_t>(strlen(m_cmd_str)));
}

void FlashProgrammer::disconnected(void)
{
    if (rw_in_progress != RW_NONE) {
        std::cout << std::endl << "== ERROR";
    } else {
        std::cout << "== ERROR" << std::endl;
    }

    stop();
}

void FlashProgrammer::stop(void)
{
    if (rw_in_progress != RW_NONE) {
        std::cout << std::endl;
    }

    emit finished(ERR_ABORT);
}

void FlashProgrammer::start(int argc, char *argv[])
{
    m_arg = argv;
    std::cout << std::unitbuf;

    QBluetoothAddress bdaddr = QBluetoothAddress(m_arg[1]);
    QString command = QString(m_arg[2]);

    m_device = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_device, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(m_device, SIGNAL(connected()), this, SLOT(connected()));
    connect(m_device, SIGNAL(disconnected()), this, SLOT(disconnected()));
    connect(m_device, SIGNAL(error(QBluetoothSocket::SocketError)), this, SLOT(disconnected()));

    if (command == "erase_all" && argc == 3) {
        m_cmd_idx = CMD_IDX_ERASE_ALL;
        snprintf(m_cmd_str, sizeof(m_cmd_str), CMD_FMT_ERASE_ALL"\r\n");

        m_device->connectToService(bdaddr, QBluetoothUuid::SerialPort);
    } else if (command == "erase" && argc == 5) {
        uint32_t addr = static_cast<uint32_t>(std::stoul(m_arg[3], nullptr, 16));
        uint32_t length = static_cast<uint32_t>(std::stoul(argv[4], nullptr, 16));

        if (length <= 0) {
            std::cout << "invalid length" << std::endl;
            emit finished(ERR_ARG);
            return;
        }

        m_cmd_idx = CMD_IDX_ERASE;
        snprintf(m_cmd_str, sizeof(m_cmd_str), CMD_FMT_ERASE"\r\n", addr, length);

        m_device->connectToService(bdaddr, QBluetoothUuid::SerialPort);
    } else if (command == "write" && argc == 6) {
        uint32_t addr = static_cast<uint32_t>(std::stoul(m_arg[3], nullptr, 16));
        uint32_t length = static_cast<uint32_t>(std::stoul(argv[4], nullptr, 16));

        if (length <= 0) {
            std::cout << "Invalid length" << std::endl;
            emit finished(ERR_ARG);
            return;
        }

        data_fd = new QFile(m_arg[5]);
        if (!data_fd->open(QIODevice::ReadOnly)) {
            std::cout << "Could not open file" << std::endl;
            emit finished(ERR_FILE);
            return;
        }
        data_size = length;

        m_cmd_idx = CMD_IDX_WRITE;
        snprintf(m_cmd_str, sizeof(m_cmd_str), CMD_FMT_WRITE"\r\n", addr, length);

        m_device->connectToService(bdaddr, QBluetoothUuid::SerialPort);
    } else if (command == "read" && argc == 6) {
        uint32_t addr = static_cast<uint32_t>(std::stoul(m_arg[3], nullptr, 16));
        uint32_t length = static_cast<uint32_t>(std::stoul(argv[4], nullptr, 16));

        if (length <= 0) {
            std::cout << "Invalid length" << std::endl;
            emit finished(ERR_ARG);
            return;
        }

        data_fd = new QFile(m_arg[5]);
        if (!data_fd->open(QIODevice::WriteOnly)) {
            std::cout << "Could not open file" << std::endl;
            emit finished(ERR_FILE);
            return;
        }
        data_size = length; data_recv = 0;

        m_cmd_idx = CMD_IDX_READ;
        snprintf(m_cmd_str, sizeof(m_cmd_str), CMD_FMT_READ"\r\n", addr, length);

        m_device->connectToService(bdaddr, QBluetoothUuid::SerialPort);
    } else if (command == "info" && argc == 3) {
        m_cmd_idx = CMD_IDX_INFO;
        snprintf(m_cmd_str, sizeof(m_cmd_str), CMD_FMT_INFO"\r\n");

        m_device->connectToService(bdaddr, QBluetoothUuid::SerialPort);
    } else {
        printUsage();
    }
}
