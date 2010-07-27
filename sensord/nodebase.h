/**
   @file nodebase.h
   @brief Base class for all nodes in filtering chain

   <p>
   Copyright (C) 2009-2010 Nokia Corporation

   @author Timo Rongas <ext-timo.2.rongas@nokia.com>

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

#ifndef NODEBASE_H
#define NODEBASE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMutex>
#include "datarange.h"

/**
 * Base class for all nodes in sensord framework filtering chain.
 * Provides common functions for handling node metadata.
 *
 * @todo Improve handling of local vs. source originated data.
 */
class NodeBase : public QObject
{
    Q_OBJECT;
    Q_PROPERTY(QString description READ description);
    Q_PROPERTY(bool standbyOverride READ standbyOverride);

protected:
    NodeBase(QObject* parent=0) : QObject(parent), m_dataRangeSource(NULL) {}
    virtual ~NodeBase() {}

public Q_SLOTS:

    /**
     * Get the description for this node.
     * @return Descriptive string.
     */
    QString description() const;

    /**
     * Remove a range request.
     * @param sessionID ID of the session whose request to remove.
     */
    void removeDataRangeRequest(int sessionId);

    /**
     * Get list of available data ranges for this node.
     * @return QList of available data ranges for this sensor.
     */
    QList<DataRange> getAvailableDataRanges() const;

    /**
     * Get the currently active DataRange for this node.
     * @return DataRangeRequest currently in use. If no requests are set
     *         id will be set to -1.
     */
    DataRangeRequest getCurrentDataRange() const;

    /**
     * Places a request for the given data range into queue. The range
     * will be activated once all earlier requests have been released.
     *
     * If the range is not valid, the request will be dropped
     * immediately.
     *
     * @param sessionId session ID for the client making the request.
     * @param range The requested data range
     */
    void requestDataRange(int sessionId, DataRange range);

    /**
     * Tells whether the node is in standbyOverride mode. If \c true, the
     * dataflow from the sensor will continue even if the device display
     * goes blank. Any client making the request for a sensor will make
     * all clients of the sensor experience the same behavior.
     *
     * <br/><br/><b>!! NOTE !! </b>: Due to the fact that any other
     * client may request this property for another sensor, whose processing
     * chain is partly shared with the sensor this client is using, it is
     * possible to receive samples only from some sources (i.e. this sensor
     * has the property off, the one one has it on --> we get samples
     * from the shared parts). This may lead to some unexpected behavior.
     *
     * Return value is based on whether all source nodes are or local
     * implementation is true.
     * @return \c true if standyOverride is on, \c false otherwise.
     */
    bool standbyOverride() const;

    /**
     * Sets a request for standbyOverride property.
     *
     * @param sessionId ID of the session making the request. Used for
     *                  maintenance. Always supply valid ID.
     * @param override Whether to request (\c true) or reset (\c false).
     * @return \c true if local implementation or all sources return true.
     *         \c false otherwise.
     */
    bool setStandbyOverrideRequest(const int sessionId, const bool override);

Q_SIGNALS:
    void propertyChanged(const QString& name);

protected:

    /**
     * Validates the metadata setup for the node. To pass, exactly one
     * of the following conditions must be fullfilled for each propagative
     * property.
     * <ol>
     *   <li>Node on the previous layer has been marked as a source</li>
     *   <li>At least one value has been given for this node</li>
     * </ol>
     *
     * This function should be placed to the end of each node constructor.
     *
     * @return \c true on valid metadata setup, \c false otherwise.
     */
    bool isMetadataValid() const;

    /**
     * Set the description for the node.
     * @param str The description string to set.
     */
    void setDescription(const QString str);

    /**
     * Introduce a new available range. Locally defined range will
     * override any ranges given by previous layers in the filtering
     * chain.
     *
     * This function is expected to be used only during node construction.
     * Introduced ranges can not be removed.
     *
     * @param range The range to add.
     */
    void introduceAvailableDataRange(const DataRange& range);

    /**
     * Sets the node to request range information from in case local
     * values have not been set.
     *
     * @param node The node to request range metadata from.
     */
    void setRangeSource(NodeBase* node);

    /**
     * Sets the current datarange. Overwrite for nodes which have more
     * than one possible range and are responsible for actions to change
     * it. For nodes which merely get several options from previous nodes
     * this won't get invoked.
     * @param range The range to set
     * @return \c true on succesfull set, \c false otherwise. The base
     *         class implementation always returns false.
     *
     * @todo There's a serious design flaw here. If a node is to reimplement
     *       this, and forward requests to selected sources, the function
     *       is not aware of the client id whose request is handled.
     */
    virtual bool setDataRange(const DataRange range, const int sessionId) { Q_UNUSED(range); Q_UNUSED(sessionId); return false; }

    /**
     * Sets the standbyOverride value for the node. This is the base
     * implementation, which should be reimplemented for any node that
     * wishes to provide real functionality.
     *
     * @param override \c true to set standbyOverride on, \c false to
     *        remove the request.
     * @return Whether request was successfull. For this implementation,
     *         always \c false.
     */
    virtual bool setStandbyOverride(const bool override) { Q_UNUSED(override); return false; }

    /**
     * Adds a new node to the list of nodes that standbyOverride calls
     * are forwarded to. Each node in the list gets the requests sent
     * to this node.
     *
     * Should be called by nodes that need information on standbyOverride
     * from nodes in the previous layer (i.e. everything but adaptors).
     *
     * @param node Node to append to the list.
     */
    void addStandbyOverrideSource(NodeBase* node);

private:

    /**
     * Returns whether the class defines its own output data range, or
     * whether it uses the values from previous layer.
     *
     * @return \c true if range defined locally, \c false otherwise
     */
    bool hasLocalRange() const;

    QString                 m_description;
    QList<DataRange>        m_dataRangeList;
    QList<DataRangeRequest> m_dataRangeQueue;
    NodeBase*               m_dataRangeSource;

    QList<NodeBase*>        m_standbySourceList;
    QList<int>              m_standbyRequestList;

    mutable QMutex          m_mutex;
};

#endif