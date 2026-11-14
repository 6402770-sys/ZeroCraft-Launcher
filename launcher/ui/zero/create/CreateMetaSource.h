// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - where Create Instance gets its version lists from. The dialog only talks to `MetaSource`, so tests hand it
// synthetic lists (no network); the app uses `PrismMetaSource`, which loads Prism's cached meta lists
// (APPLICATION->metadataIndex()) through their own load tasks.
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include "meta/Version.h"
#include "tasks/Task.h"

namespace Zero {
namespace Create {

class MetaSource : public QObject {
    Q_OBJECT
   public:
    enum class Status { Idle, Loading, Ready, Failed };

    using QObject::QObject;
    ~MetaSource() override = default;

    virtual Status status(const QString& uid) const = 0;
    /// Why the last load of `uid` failed (empty unless Failed).
    virtual QString errorText(const QString& uid) const = 0;
    /// Starts loading `uid` unless it is Ready or Loading. A Failed list is loaded again (Retry).
    virtual void request(const QString& uid) = 0;
    /// The versions of a Ready list (empty otherwise).
    virtual QList<Meta::Version::Ptr> versions(const QString& uid) const = 0;

   signals:
    /// `uid` changed status (Loading, Ready or Failed).
    void statusChanged(const QString& uid);
};

/// Prism's meta lists. Loads run one at a time: every list's load task also (re)loads the shared meta index, and
/// starting that index task while another list's task runs it would trip Task's "already running" assertion.
class PrismMetaSource : public MetaSource {
    Q_OBJECT
   public:
    explicit PrismMetaSource(QObject* parent = nullptr);
    ~PrismMetaSource() override;

    Status status(const QString& uid) const override;
    QString errorText(const QString& uid) const override;
    void request(const QString& uid) override;
    QList<Meta::Version::Ptr> versions(const QString& uid) const override;

   private:
    void startNext();

    QStringList m_queue;
    QString m_running;
    Task::Ptr m_task;
    QHash<QString, QString> m_errors;
};

}  // namespace Create
}  // namespace Zero
