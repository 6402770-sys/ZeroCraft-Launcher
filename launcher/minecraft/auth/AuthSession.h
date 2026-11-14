#pragma once

#include <QString>
#include <memory>

#include "LaunchMode.h"

class MinecraftAccount;

struct AuthSession {
    bool MakeOffline(QString offline_playername);
    void MakeDemo(QString name, QString uuid);

    QString serializeUserProperties();

    // combined session ID
    QString session;
    // volatile auth token
    QString access_token;
    // profile name
    QString player_name;
    // profile ID
    QString uuid;
    // 'msa' or 'offline', depending on account type
    QString user_type;
    // the actual launch mode for this session
    LaunchMode launchMode;
    // ZERO: Ely.by accounts play through the authlib-injector java agent (launcher/zero/auth/AuthlibInjector.h). Set by
    // MinecraftAccount::fillSession; cleared for offline/demo sessions. The launch step EnsureAuthlibInjector fills the jar path.
    bool wantsAuthlibInjector = false;
    QString authlibInjectorJar;
};

using AuthSessionPtr = std::shared_ptr<AuthSession>;
