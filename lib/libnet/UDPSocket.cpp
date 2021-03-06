/*
 * Copyright (C) 2015 Niek Linnenbank
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ByteOrder.h>
#include <Randomizer.h>
#include "Ethernet.h"
#include "UDP.h"
#include "UDPSocket.h"

UDPSocket::UDPSocket(const u32 inode,
                     UDP *udp,
                     const ProcessID pid)
    : NetworkSocket(inode, udp->getMaximumPacketSize(), pid)
    , m_udp(udp)
    , m_port(0)
    , m_queue(udp->getMaximumPacketSize())
{
}

UDPSocket::~UDPSocket()
{
}

const u16 UDPSocket::getPort() const
{
    return m_port;
}

FileSystem::Result UDPSocket::read(IOBuffer & buffer,
                                   Size & size,
                                   const Size offset)
{
    DEBUG("");

    NetworkQueue::Packet *pkt = m_queue.pop();
    if (!pkt)
    {
        return FileSystem::RetryAgain;
    }

    IPV4::Header *ipHdr = (IPV4::Header *)(pkt->data + sizeof(Ethernet::Header));
    UDP::Header *udpHdr = (UDP::Header *)(pkt->data + sizeof(Ethernet::Header) + sizeof(IPV4::Header));
    NetworkClient::SocketInfo info;
    Size payloadSize = pkt->size - sizeof(Ethernet::Header)
                                 - sizeof(IPV4::Header)
                                 - sizeof(UDP::Header);

    // Fill socket info
    info.address = readBe32(&ipHdr->source);
    info.port    = readBe16(&udpHdr->sourcePort);
    buffer.write(&info, sizeof(info));

    // Fill payload
    Size sz = size > payloadSize ? payloadSize : size;
    buffer.write(udpHdr+1, sz, sizeof(info));
    m_queue.release(pkt);
    size = sz + sizeof(info);

    return FileSystem::Success;
}

FileSystem::Result UDPSocket::write(IOBuffer & buffer,
                                    Size & size,
                                    const Size offset)
{
    DEBUG("");

    // Receive socket information first?
    if (!m_info.port)
    {
        buffer.read(&m_info, sizeof(m_info));

        if (m_info.port == 0)
        {
            Randomizer rand;
            m_info.port = rand.next() % 65535;
        }

        DEBUG("addr =" << m_info.address << " port = " << m_info.port);

        return m_udp->bind(this, m_info.port);
    }
    else
    {
        return m_udp->sendPacket(&m_info, buffer, size);
    }
}

bool UDPSocket::canRead() const
{
    return m_queue.hasData();
}

FileSystem::Result UDPSocket::process(const NetworkQueue::Packet *pkt)
{
    DEBUG("");

    NetworkQueue::Packet *buf = m_queue.get();
    if (!buf)
    {
        ERROR("udp socket queue full");
        return FileSystem::IOError;
    }

    buf->size = pkt->size;
    MemoryBlock::copy(buf->data, pkt->data, pkt->size);
    m_queue.push(buf);

    return FileSystem::Success;
}
