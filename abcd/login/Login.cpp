/*
 * Copyright (c) 2014, AirBitz, Inc.
 * All rights reserved.
 *
 * See the LICENSE file for more information.
 */

#include "Login.hpp"
#include "Lobby.hpp"
#include "LoginDir.hpp"
#include "LoginServer.hpp"
#include "../crypto/Encoding.hpp"
#include "../crypto/Random.hpp"
#include "../json/JsonBox.hpp"
#include "../util/FileIO.hpp"
#include "../util/Sync.hpp"
#include "../util/Util.hpp"
#include <ctype.h>

namespace abcd {

#define ACCOUNT_MK_LENGTH 32

Login::Login(std::shared_ptr<Lobby> parent, DataSlice dataKey):
    lobby(*parent),
    parent_(std::move(parent)),
    dataKey_(dataKey.begin(), dataKey.end())
{}

Status
Login::init(const LoginPackage &loginPackage)
{
    DataChunk syncKey;
    ABC_CHECK(loginPackage.syncKeyBox().decrypt(syncKey, dataKey_));
    syncKey_ = base16Encode(syncKey);

    // Ensure that the directory is in place:
    ABC_CHECK(lobby.dirCreate());
    return Status();
}

/**
 * Creates a new login account, both on-disk and on the server.
 *
 * @param szUserName    The user name for the account.
 * @param szPassword    The password for the account.
 * @param ppSelf        The returned login object.
 */
tABC_CC ABC_LoginCreate(std::shared_ptr<Login> &result,
                        std::shared_ptr<Lobby> lobby,
                        const char *szPassword,
                        tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    std::unique_ptr<Login> login;
    CarePackage carePackage;
    LoginPackage loginPackage;
    DataChunk authKey;          // Unlocks the server
    DataChunk passwordKey;      // Unlocks dataKey
    DataChunk dataKey;          // Unlocks the account
    DataChunk syncKey;
    JsonBox box;
    JsonSnrp snrp;

    // Generate SNRP2:
    ABC_CHECK_NEW(snrp.create(), pError);
    ABC_CHECK_NEW(carePackage.snrp2Set(snrp), pError);

    // Generate MK:
    ABC_CHECK_NEW(randomData(dataKey, ACCOUNT_MK_LENGTH), pError);

    // Generate SyncKey:
    ABC_CHECK_NEW(randomData(syncKey, SYNC_KEY_LENGTH), pError);

    if (szPassword)
    {
        std::string LP = lobby->username() + szPassword;

        // Generate authKey:
        ABC_CHECK_NEW(usernameSnrp().hash(authKey, LP), pError);

        // Set up EMK_LP2:
        ABC_CHECK_NEW(carePackage.snrp2().hash(passwordKey, LP), pError);
        ABC_CHECK_NEW(box.encrypt(dataKey, passwordKey), pError);
        ABC_CHECK_NEW(loginPackage.passwordBoxSet(box), pError);
    }
    else
    {
        // Generate authKey:
        ABC_CHECK_NEW(randomData(authKey, scryptDefaultSize), pError);
    }

    // Encrypt authKey:
    ABC_CHECK_NEW(box.encrypt(authKey, dataKey), pError);
    ABC_CHECK_NEW(loginPackage.authKeyBoxSet(box), pError);

    // Set up ESyncKey:
    ABC_CHECK_NEW(box.encrypt(syncKey, dataKey), pError);
    ABC_CHECK_NEW(loginPackage.syncKeyBoxSet(box), pError);

    // Create the account and repo on server:
    ABC_CHECK_RET(ABC_LoginServerCreate(*lobby, toU08Buf(authKey),
        carePackage, loginPackage, base16Encode(syncKey).c_str(), pError));

    // Create the Login object:
    login.reset(new Login(lobby, dataKey));
    ABC_CHECK_NEW(login->init(loginPackage), pError);

    // Latch the account:
    ABC_CHECK_RET(ABC_LoginServerActivate(*lobby, toU08Buf(authKey), pError));

    // Set up the on-disk login:
    ABC_CHECK_NEW(carePackage.save(lobby->carePackageName()), pError);
    ABC_CHECK_NEW(loginPackage.save(lobby->loginPackageName()), pError);

    // Assign the result:
    result.reset(login.release());

exit:
    return cc;
}

/**
 * Obtains the account's server authentication key.
 * @param pLP1      The hashed user name & password. The caller must free this.
 */
tABC_CC ABC_LoginGetServerKey(const Login &login,
                               tABC_U08Buf *pLP1,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    LoginPackage loginPackage;
    DataChunk authKey;          // Unlocks the server

    ABC_CHECK_NEW(loginPackage.load(login.lobby.loginPackageName()), pError);
    ABC_CHECK_NEW(loginPackage.authKeyBox().decrypt(authKey, login.dataKey()), pError);
    ABC_BUF_DUP(*pLP1, toU08Buf(authKey));

exit:
    return cc;
}

} // namespace abcd
