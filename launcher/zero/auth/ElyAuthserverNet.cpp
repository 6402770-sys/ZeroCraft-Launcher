// SPDX-License-Identifier: GPL-3.0-only
#include "ElyAuthserverNet.h"

#include <QCoreApplication>
#include <memory>

#include "Application.h"
#include "net/NetJob.h"
#include "net/RawHeaderProxy.h"
#include "net/Request.h"

namespace Zero::Auth::Ely::Authserver {

namespace {

/// Everything one request needs to stay alive until it finished. It keeps itself alive through `self` until the answer was delivered.
struct InFlight {
    NetJob::Ptr job;
    Net::Request::Ptr request;
    QByteArray* response = nullptr;
    Done done;
    bool delivered = false;
    std::shared_ptr<InFlight> self;
};

}  // namespace

Transport networkTransport()
{
    return [](const QUrl& url, const QByteArray& body, Done done) -> Cancel {
        auto* app = qobject_cast<Application*>(QCoreApplication::instance());
        if (!app) {
            HttpReply none;
            none.errorString = QStringLiteral("no application");
            done(none);
            return {};
        }

        auto [request, response] = Net::Request::makeByteArray(url, body);
        request->addHeaderProxy(std::make_unique<Net::RawHeaderProxy>(QList<Net::HeaderPair>{
            { "Content-Type", "application/json" },
            { "Accept", "application/json" },
        }));

        auto state = std::make_shared<InFlight>();
        state->request = request;
        state->response = response;
        state->done = std::move(done);
        state->job.reset(new NetJob("ElyAuthserver", app->network()));
        state->job->setAskRetry(false);
        state->job->addNetAction(request);
        state->self = state;

        std::weak_ptr<InFlight> weak = state;
        QObject::connect(state->job.get(), &Task::finished, state->job.get(), [weak]() {
            auto s = weak.lock();
            if (!s || s->delivered)
                return;
            s->delivered = true;
            HttpReply reply;
            reply.status = s->request->replyStatusCode();
            reply.answered = reply.status > 0;
            reply.error = s->request->error();
            reply.errorString = s->request->errorString();
            if (s->response)
                reply.body = *s->response;
            auto callback = std::move(s->done);
            s->self.reset();  // `s` is the last reference: the job is deleted (later) when this slot returns
            if (callback)
                callback(std::move(reply));
        });
        state->job->start();
        return [weak]() {
            if (auto s = weak.lock(); s && s->job && !s->delivered)
                s->job->abort();
        };
    };
}

void invalidateInBackground(const QString& accessToken, const QString& clientToken, Transport transport)
{
    if (accessToken.isEmpty() || !qobject_cast<Application*>(QCoreApplication::instance()))
        return;
    if (!transport)
        transport = networkTransport();
    // the answer is ignored; the cancel handle is dropped (the request runs to its end on its own)
    transport(url(QStringLiteral("invalidate")), invalidateBody(accessToken, clientToken), [](HttpReply) {});
}

}  // namespace Zero::Auth::Ely::Authserver
