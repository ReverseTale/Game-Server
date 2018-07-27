
#######################
# ReverseTale-Library

    RequireExternal(
        TARGET ReverseTale-GameServer
        MODULE "ReverseTale/Library:master"
        INSTALL_NAME ReverseTale-Library
    )

#######################
# ReverseTale-Common-Server

    RequireExternal(
        TARGET ReverseTale-GameServer
        MODULE "ReverseTale/Common-Server:master"
        INSTALL_NAME ReverseTale-CommonServer
    )
