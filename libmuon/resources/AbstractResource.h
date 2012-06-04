/***************************************************************************
 *   Copyright © 2012 Aleix Pol Gonzalez <aleixpol@blue-systems.com>       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef ABSTRACTRESOURCE_H
#define ABSTRACTRESOURCE_H

#include <QtCore/QObject>

#include "libmuonprivate_export.h"

class MUONPRIVATE_EXPORT AbstractResource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString packageName READ packageName CONSTANT)
    Q_PROPERTY(QString comment READ comment CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
    Q_PROPERTY(bool canExecute READ canExecute CONSTANT)
    public:
        explicit AbstractResource(QObject* parent = 0);
        
        ///used as internal identification of a resource
        virtual QString packageName() const = 0;
        
        ///resource name to be displayed
        virtual QString name() = 0;
        
        ///short description of the resource
        virtual QString comment() = 0;
        
        ///xdg-compatible icon name to represent the resource
        virtual QString icon() const = 0;
        
        ///@returns whether invokeApplication makes something
        /// false if not overriden
        virtual bool canExecute() const;
};

#endif // ABSTRACTRESOURCE_H
