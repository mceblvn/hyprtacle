/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "PipeWriter.h"

#include "Logging.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

namespace
{
// How long to wait for the client to start reading before giving up.
constexpr int s_writeTimeoutMs = 60000;
}

PipeWriter::PipeWriter(int fileDescriptor, QByteArray data)
    : m_fileDescriptor(fileDescriptor)
    , m_data(std::move(data))
{
    setAutoDelete(true);
}

PipeWriter::~PipeWriter()
{
    if (m_fileDescriptor != -1) {
        ::close(m_fileDescriptor);
        m_fileDescriptor = -1;
    }
}

void PipeWriter::run()
{
    if (m_fileDescriptor == -1) {
        return;
    }

    const int flags = ::fcntl(m_fileDescriptor, F_GETFL, 0);
    if (flags != -1) {
        ::fcntl(m_fileDescriptor, F_SETFL, flags | O_NONBLOCK);
    }

    pollfd pollDescriptor;
    pollDescriptor.fd = m_fileDescriptor;
    pollDescriptor.events = POLLOUT;
    pollDescriptor.revents = 0;

    qint64 offset = 0;
    const qint64 size = m_data.size();

    while (offset < size) {
        const int ready = ::poll(&pollDescriptor, 1, s_writeTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            qCWarning(lcHyprtacle) << "poll() failed:" << strerror(errno);
            break;
        }
        if (ready == 0) {
            qCWarning(lcHyprtacle) << "Timed out writing screenshot to the client";
            break;
        }
        if (!(pollDescriptor.revents & POLLOUT)) {
            qCWarning(lcHyprtacle) << "The client closed the pipe before the screenshot was written";
            break;
        }

        const ssize_t written = ::write(m_fileDescriptor, m_data.constData() + offset, static_cast<size_t>(size - offset));
        if (written < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            qCWarning(lcHyprtacle) << "write() failed:" << strerror(errno);
            break;
        }
        if (written == 0) {
            break;
        }

        offset += written;
    }

    if (offset == size) {
        qCDebug(lcHyprtacle) << "Wrote" << size << "bytes of screenshot data to the client";
    }

    ::close(m_fileDescriptor);
    m_fileDescriptor = -1;
}
