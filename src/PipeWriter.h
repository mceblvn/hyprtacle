/*
 * SPDX-FileCopyrightText: 2026 hyprtacle contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <QByteArray>
#include <QRunnable>

/**
 * Writes a buffer to a pipe file descriptor without blocking the caller.
 *
 * The client (Spectacle) only starts reading the pipe after it has received
 * the D-Bus reply, and a pipe buffer holds far less than a full screenshot.
 * The data therefore has to be pushed from a separate thread, otherwise the
 * service would block forever waiting for a reader that is not there yet.
 */
class PipeWriter : public QRunnable
{
public:
    PipeWriter(int fileDescriptor, QByteArray data);
    ~PipeWriter() override;

    void run() override;

private:
    int m_fileDescriptor = -1;
    QByteArray m_data;
};
