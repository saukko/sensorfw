/**
   @file sockethandler.cpp
   @brief SocketHandler

   <p>
   Copyright (C) 2009-2010 Nokia Corporation

   @author Timo Rongas <ext-timo.2.rongas@nokia.com>
   @author Ustun Ergenoglu <ext-ustun.ergenoglu@nokia.com>

   This file is part of Sensord.

   Sensord is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Sensord is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Sensord.  If not, see <http://www.gnu.org/licenses/>.
   </p>
 */

#include <QLocalSocket>
#include <QLocalServer>
#include <sys/socket.h>
#include "logging.h"
#include "sockethandler.h"
#include <unistd.h>
#include <limits.h>

SessionData::SessionData(QLocalSocket* socket, QObject* parent) : QObject(parent),
                                                                  socket(socket),
                                                                  interval(-1),
                                                                  buffer(0),
                                                                  size(0)
{
    lastWrite.tv_sec = 0;
    lastWrite.tv_usec = 0;
    connect(&timer, SIGNAL(timeout()), this, SLOT(delayedWrite()));
}

SessionData::~SessionData()
{
    timer.stop();
    delete[] buffer;
    delete socket;
}

long SessionData::sinceLastWrite() const
{
    if(lastWrite.tv_sec == 0)
        return LONG_MAX;
    struct timeval now;
    gettimeofday(&now, 0);
    return (now.tv_sec - lastWrite.tv_sec) * 1000 + ((now.tv_usec - lastWrite.tv_usec) / 1000);
}

bool SessionData::write(const void* source, int size)
{
    if(interval == -1)
    {
        sensordLogT() << "[SocketHandler]: pass-through. interval not set";
        gettimeofday(&lastWrite, 0);
        return (socket->write((const char*)source, size) < 0 ? false : true);
    }
    long since = sinceLastWrite();
    if(since >= interval)
    {
        sensordLogT() << "[SocketHandler]: pass-through. since > interval";
        gettimeofday(&lastWrite, 0);
        return (socket->write((const char*)source, size) < 0 ? false : true);
    }
    else
    {
        if(!buffer)
            buffer = new char[size];
        else if(size != this->size)
        {
            if(buffer)
                delete[] buffer;
            buffer = new char[size];
        }
        this->size = size;
        memcpy(buffer, source, size);
        if(timer.timerId() != -1)
        {
            sensordLogT() << "[SocketHandler]: delayed write by " << (interval - since) << "ms";
            timer.start(interval - since);
        }
        else
        {
            sensordLogT() << "[SocketHandler]: timer already running";
        }
    }
    return true;
}

void SessionData::delayedWrite()
{
    timer.stop();
    gettimeofday(&lastWrite, 0);
    socket->write(buffer, size);
}

QLocalSocket* SessionData::stealSocket()
{
    QLocalSocket* tmpsocket = socket;
    socket = 0;
    return tmpsocket;
}

void SessionData::setInterval(int interval)
{
    this->interval = interval;
}

SocketHandler::SocketHandler(QObject* parent) : QObject(parent), m_server(NULL)
{
    m_server = new QLocalServer(this);
    connect(m_server, SIGNAL(newConnection()), this, SLOT(newConnection()));
}

SocketHandler::~SocketHandler()
{
    if (m_server) {
        delete m_server;
    }
}

bool SocketHandler::listen(QString serverName)
{
    if (m_server->isListening()) {
        sensordLogW() << "[SocketHandler]: Already listening";
        return false;
    }

    bool unlinkDone = false;
    while (!m_server->listen(serverName) && !unlinkDone && serverName[0] == QChar('/'))
    {
        if ( unlink(serverName.toLocal8Bit().constData()) == 0) {
            sensordLogD() << "[SocketHandler]: Unlinked stale socket" << serverName;
        } else {
            sensordLogD() << m_server->errorString();
        }
        unlinkDone = true;
    }
    return m_server->isListening();
}

bool SocketHandler::write(int id, const void* source, int size)
{
    // TODO: Calculate failed writes (some are expected if sockets initialize too slow)
    QMap<int, SessionData*>::iterator it = m_idMap.find(id);
    if (it == m_idMap.end())
    {
        sensordLogD() << "[SocketHandler]: Trying to write to nonexistent session (normal, no panic).";
        return false;
    }
    sensordLogT() << "[SocketHandler]: Writing to session " << id;
    return (*it)->write(source, size);
}

bool SocketHandler::removeSession(int sessionId)
{
    if (!(m_idMap.keys().contains(sessionId))) {
        sensordLogD() << "[SocketHandler]: Trying to remove nonexistent session.";
    }

    QLocalSocket* socket = (*m_idMap.find(sessionId))->stealSocket();

    if (socket) {
        disconnect(socket, SIGNAL(readyRead()), this, SLOT(socketReadable()));
        disconnect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));

        // NOTE NOTE NOTE NOTE NOTE NOTE KLUDGE KLUDGE
        // Due to some timing issues that we have not been able to figure out,
        // deleting the socket right away does not work if the session is closed
        // after a client has been lost. Thus, socket deletion is pushed forward
        // 2 seconds to allow all Qt internal async stuff to finish..
        //delete socket;
        // NOTE NOTE NOTE NOTE NOTE NOTE KLUDGE KLUDGE
        m_tmpSocks.append(socket);
        QTimer::singleShot(2000, this, SLOT(killSocket()));
    }

    delete m_idMap.take(sessionId);

    return true;
}

void SocketHandler::newConnection()
{
    sensordLogT() << "[SocketHandler]: New connection received.";

    while (m_server->hasPendingConnections()) {

        QLocalSocket* socket = m_server->nextPendingConnection();
        connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadable()));
        connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));

        /// Do an initial write to instantiate the QObject child (why
        /// this happens in the write operation and not above is a
        /// mystery to me). This chunk provide info about datatype..
        // TODO: Might not need this now that we first receive a write from the other end.
        socket->write("_SENSORCHANNEL_", 16);
        socket->flush();
    }
}

void SocketHandler::socketReadable()
{
    int sessionId = -1;
    QLocalSocket* socket = (QLocalSocket*)sender();
    ((QLocalSocket*)sender())->read((char*)&sessionId, sizeof(int));

    disconnect(socket, SIGNAL(readyRead()), this, SLOT(socketReadable()));

    if (sessionId >= 0) {
        if(!m_idMap.contains(sessionId))
            m_idMap.insert(sessionId, new SessionData((QLocalSocket*)sender(), this));
    } else {
        // TODO: Handle in a clean way, don't die.
        sensordLogC() << "[SocketHandler]: Failed to read valid session ID from client.";
        Q_ASSERT(false);
    }
}

void SocketHandler::socketDisconnected()
{
    QLocalSocket* socket = (QLocalSocket*)sender();

    int sessionId = -1;
    for(QMap<int, SessionData*>::const_iterator it = m_idMap.begin(); it != m_idMap.end(); ++it)
    {
        if(it.value()->getSocket() == socket)
            sessionId = it.key();
    }

    if (sessionId == -1) {
        sensordLogW() << "[SocketHandler]: Noticed lost session, but can't find it.";
        return;
    }

    emit lostSession(sessionId);
}

void SocketHandler::killSocket()
{
    if (m_tmpSocks.size() > 0) {
        sensordLogT() << "[SocketHandler]: Deleting socket pointer:" << m_tmpSocks.at(0);
        delete m_tmpSocks.takeAt(0);
    } else {
        sensordLogW() << "[SocketHandler]: Ugly hack just went bad.. attempting to delete nonexisting pointer.";
    }
}

int SocketHandler::getSocketFd(int sessionId) const
{
    QMap<int, SessionData*>::const_iterator it = m_idMap.find(sessionId);
    if (it != m_idMap.end())
        return (*it)->getSocket()->socketDescriptor();
    return 0;
}

void SocketHandler::setInterval(int sessionId, int value)
{
    QMap<int, SessionData*>::iterator it = m_idMap.find(sessionId);
    if (it != m_idMap.end())
        (*it)->setInterval(value);
}

void SocketHandler::clearInterval(int sessionId)
{
    QMap<int, SessionData*>::iterator it = m_idMap.find(sessionId);
    if (it != m_idMap.end())
        m_idMap.erase(it);
}
