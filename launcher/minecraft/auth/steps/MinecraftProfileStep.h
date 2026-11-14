#pragma once
#include <QObject>

#include "minecraft/auth/AuthStep.h"
#include "net/Request.h"
#include "net/NetJob.h"

class MinecraftProfileStep : public AuthStep {
    Q_OBJECT

   public:
    explicit MinecraftProfileStep(AccountData* data);
    virtual ~MinecraftProfileStep() noexcept = default;

    void perform() override;

    QString describe() override;

   protected:
    // ZERO: the Ely.by step reads the profile from Ely's Mojang-compatible endpoint
    QUrl m_profileUrl{ "https://api.minecraftservices.com/minecraft/profile" };
    //! ZERO: called after a profile was read and parsed; the Ely.by step marks the account as owning the game there
    virtual void profileAcquired() {}

   private slots:
    void onRequestDone(QByteArray* response);

   private:
    Net::Request::Ptr m_request;
    NetJob::Ptr m_task;
};
