#include "worker.h"
#include <QDebug>
#include <QCoreApplication>
#include <QFileInfo>

/*
 *----------------------------------------------------------------------
 *
 * InitBuffer --
 *
 *      Fill an array of uint32 with random values, to defeat any
 *      attempts to compress it.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::InitBuffer(uint32 *buf, uint32 numElems)
{
    int i;

    srand(time(NULL));

    for (i = 0; i < numElems; i++) {
        buf[i] = (uint32)rand();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PrepareThreadData --
 *
 *      Open the source and destination disk for multi threaded copy.
 *
 * Results:
 *      Fills in ThreadData in td.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::PrepareThreadData(VixDiskLibConnection &dstConnection, ThreadData &td)
{
    VixError vixError;
    VixDiskLibCreateParams createParams;
    VixDiskLibInfo *info = NULL;
    string prefixName,randomFilename;

    prefixName = "c:\\test";
    GenerateRandomFilename(prefixName, randomFilename);
    td.dstDisk = randomFilename;

    vixError = VixDiskLib_Open(appGlobals.connection,
                               appGlobals.diskPath.toUtf8().constData(),
                               appGlobals.openFlags,
                               &td.srcHandle);
    CHECK_AND_THROW(vixError);

    vixError = VixDiskLib_GetInfo(td.srcHandle, &info);
    CHECK_AND_THROW(vixError);
    td.numSectors = info->capacity;
    VixDiskLib_FreeInfo(info);

    createParams.adapterType = VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC;
    createParams.capacity = td.numSectors;
    createParams.diskType = VIXDISKLIB_DISK_SPLIT_SPARSE;
    createParams.hwVersion = VIXDISKLIB_HWVERSION_WORKSTATION_5;

    vixError = VixDiskLib_Create(dstConnection, td.dstDisk.c_str(),
                                 &createParams, NULL, NULL);
    CHECK_AND_THROW(vixError);

    vixError = VixDiskLib_Open(dstConnection, td.dstDisk.c_str(), 0,
                               &td.dstHandle);
    CHECK_AND_THROW(vixError);
}

/*
 *----------------------------------------------------------------------
 *
 * PrintStat --
 *
 *      Print performance statistics for read/write benchmarks.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::PrintStat(bool read, timeval start, timeval end, uint32 numSectors)
{
    uint64 elapsed;
    uint32 speed;

    elapsed = ((uint64)end.tv_sec * 1000000 + end.tv_usec -
               ((uint64)start.tv_sec * 1000000 + start.tv_usec)) / 1000;
    if (elapsed == 0) {
       elapsed = 1;
    }
    speed = (1000 * VIXDISKLIB_SECTOR_SIZE * (uint64)numSectors) / (1024 * 1024 * elapsed);
    printf("%s %d MBytes in %d msec (%d MBytes/sec)\n", read ? "Read" : "Wrote",
           (uint32)(numSectors /(2048)), (uint32)elapsed, speed);
}

//definition for static members

WorkerConfig worker::appGlobals;
VixDiskLibConnectParams worker::cnxParams;
bool worker::bVixInit;


worker::worker()
{
    m_thread = new QThread(this);

    bVixInit = false;

    appGlobals.command = 0;
    appGlobals.adapterType = VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC;
    appGlobals.startSector = 0;
    appGlobals.numSectors = 1;
    appGlobals.mbSize = 100;
    appGlobals.filler = 0xff;
    appGlobals.openFlags = 0;
    appGlobals.numThreads = 1;
    appGlobals.success = true;
    appGlobals.isRemote = false;

    // Initialize random generator
    struct timeval time;
    gettimeofday(&time, NULL);

    srand((time.tv_sec * 1000) + (time.tv_usec/1000));

}

worker::~worker()
{
    delete m_thread;

    VixError vixError;
    if (appGlobals.vmxSpec != NULL) {
       vixError = VixDiskLib_EndAccess(&cnxParams, "Sample");
    }
    if (appGlobals.connection != NULL) {
       VixDiskLib_Disconnect(appGlobals.connection);
    }
    if (bVixInit) {
       VixDiskLib_Exit();
    }
    if (cnxParams.vmxSpec)
        delete[] cnxParams.vmxSpec;
    if (cnxParams.serverName)
        delete[] cnxParams.serverName;
    if (cnxParams.creds.uid.userName)
        delete[] cnxParams.creds.uid.userName;
    if (cnxParams.creds.uid.password)
        delete[] cnxParams.creds.uid.password;
    if (cnxParams.thumbPrint)
        delete[] cnxParams.thumbPrint;
}


/*
 *--------------------------------------------------------------------------
 *
 * ParseArguments --
 *
 *      Parses the arguments passed on the command line.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

int worker::ParseArguments(int argc, char *argv[])
{
    int i;
    if (argc < 3) {
        printf("Error: Too few arguments. See usage below.\n\n");
        return PrintUsage();
    }
    for (i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "-info")) {
            appGlobals.command |= COMMAND_INFO;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-create")) {
            appGlobals.command |= COMMAND_CREATE;
        } else if (!strcmp(argv[i], "-dump")) {
            appGlobals.command |= COMMAND_DUMP;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-fill")) {
            appGlobals.command |= COMMAND_FILL;
        } else if (!strcmp(argv[i], "-meta")) {
            appGlobals.command |= COMMAND_DUMP_META;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-single")) {
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_SINGLE_LINK;
        } else if (!strcmp(argv[i], "-adapter")) {
            if (i >= argc - 2) {
                printf("Error: The -adaptor option requires the adapter type "
                       "to be specified. The type must be 'ide' or 'scsi'. "
                       "See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.adapterType = strcmp(argv[i], "scsi") == 0 ?
                                       VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC :
                                       VIXDISKLIB_ADAPTER_IDE;
            ++i;
        } else if (!strcmp(argv[i], "-rmeta")) {
            appGlobals.command |= COMMAND_READ_META;
            if (i >= argc - 2) {
                printf("Error: The -rmeta command requires a key value to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.metaKey = argv[++i];
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-wmeta")) {
            appGlobals.command |= COMMAND_WRITE_META;
            if (i >= argc - 3) {
                printf("Error: The -wmeta command requires key and value to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.metaKey = argv[++i];
            appGlobals.metaVal = argv[++i];
        } else if (!strcmp(argv[i], "-redo")) {
            if (i >= argc - 2) {
                printf("Error: The -redo command requires the parentPath to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.command |= COMMAND_REDO;
            appGlobals.parentPath = argv[++i];
        } else if (!strcmp(argv[i], "-val")) {
            if (i >= argc - 2) {
                printf("Error: The -val option requires a byte value to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.filler = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-start")) {
            if (i >= argc - 2) {
                printf("Error: The -start option requires a sector number to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.startSector = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-count")) {
            if (i >= argc - 2) {
                printf("Error: The -count option requires the number of "
                       "sectors to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.numSectors = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-cap")) {
            if (i >= argc - 2) {
                printf("Error: The -cap option requires the capacity in MB "
                       "to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.mbSize = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-clone")) {
            if (i >= argc - 2) {
                printf("Error: The -clone command requires the path of the "
                       "source vmdk to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.srcPath = argv[++i];
            appGlobals.command |= COMMAND_CLONE;
        } else if (!strcmp(argv[i], "-readbench")) {
            if (0 && i >= argc - 2) {
                printf("Error: The -readbench command requires a block size "
                       "(in sectors) to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.bufSize = strtol(argv[++i], NULL, 0);
            appGlobals.command |= COMMAND_READBENCH;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-writebench")) {
            if (i >= argc - 2) {
                printf("Error: The -writebench command requires a block size "
                       "(in sectors) to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.bufSize = strtol(argv[++i], NULL, 0);
            appGlobals.command |= COMMAND_WRITEBENCH;
        } else if (!strcmp(argv[i], "-multithread")) {
            if (i >= argc - 2) {
                printf("Error: The -multithread option requires the number "
                       "of threads to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.command |= COMMAND_MULTITHREAD;
            appGlobals.numThreads = strtol(argv[++i], NULL, 0);
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-host")) {
            if (i >= argc - 2) {
                printf("Error: The -host option requires the IP address "
                       "or name of the host to be specified. "
                       "See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.host = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-user")) {
            if (i >= argc - 2) {
                printf("Error: The -user option requires a username "
                       "to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.userName = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-password")) {
            if (i >= argc - 2) {
                printf("Error: The -password option requires a password "
                       "to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.password = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-thumb")) {
            if (i >= argc - 2) {
                printf("Error: The -thumb option requires an SSL thumbprint "
                       "to be specified. See usage below.\n\n");
               return PrintUsage();
            }
            appGlobals.thumbPrint = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-port")) {
            if (i >= argc - 2) {
                printf("Error: The -port option requires the host's port "
                       "number to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.port = strtol(argv[++i], NULL, 0);
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-nfchostport")) {
           if (i >= argc - 2) {
              return PrintUsage();
           }
           appGlobals.nfcHostPort = strtol(argv[++i], NULL, 0);
           appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-vm")) {
            if (i >= argc - 2) {
                printf("Error: The -vm option requires the moref id of "
                       "the vm to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.vmxSpec = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-libdir")) {
           if (i >= argc - 2) {
              printf("Error: The -libdir option requires the folder location "
                     "of the VDDK installation to be specified. "
                     "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.libdir = argv[++i];
        } else if (!strcmp(argv[i], "-initex")) {
           if (i >= argc - 2) {
              printf("Error: The -initex option requires the path and filename "
                     "of the VDDK config file to be specified. "
                     "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.useInitEx = true;
           appGlobals.cfgFile = argv[++i];
           if (appGlobals.cfgFile[0] == '\0') {
              appGlobals.cfgFile = "";
           }
        } else if (!strcmp(argv[i], "-ssmoref")) {
           if (i >= argc - 2) {
              printf("Error: The -ssmoref option requires the moref id "
                       "of a VM snapshot to be specified. "
                       "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.ssMoRef = argv[++i];
        } else if (!strcmp(argv[i], "-mode")) {
            if (i >= argc - 2) {
                printf("Error: The -mode option requires a mode string to  "
                       "connect to VixDiskLib_ConnectEx. Valid modes are "
                        "'nbd', 'nbdssl', 'san' and 'hotadd'. "
                        "See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.transportModes = argv[++i];
        } else if (!strcmp(argv[i], "-check")) {
            if (i >= argc - 2) {
                printf("Error: The -check command requires a true or false "
                       "value to indicate if a repair operation should be "
                       "attempted. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.command |= COMMAND_CHECKREPAIR;
            appGlobals.repair = strtol(argv[++i], NULL, 0);
        } else {
           printf("Error: Unknown command or option: %s\n", argv[i]);
           return PrintUsage();
        }
    }
    appGlobals.diskPath = argv[i];

    if (BitCount(appGlobals.command) != 1) {
       printf("Error: Missing command. See usage below.\n");
       return PrintUsage();
    }

    if (appGlobals.isRemote) {
       if (appGlobals.host == NULL ||
           appGlobals.userName == NULL ||
           appGlobals.password == NULL) {
           printf("Error: Missing a mandatory option. ");
           printf("-host, -user and -password must be specified. ");
           printf("See usage below.\n");
           return PrintUsage();
       }
    }

    /*
     * TODO: More error checking for params, really
     */
    return 0;
}

/*--------------------------------------------------------------------------
*
* DoInit --
*
*      TBD.
*
* Results:
*      None.
*
* Side effects:
*      None.
*
*--------------------------------------------------------------------------
*/

void worker::DoInit()
{
    VixError vixError;
    try {
        if (appGlobals.isRemote) {
            CharArWrapper vmxSpec(appGlobals.vmxSpec);                  //a tricky way to copy QString to cast char* (don't forget fo delete[] allocated memory)
            cnxParams.vmxSpec = new char[vmxSpec.size()];
            strcpy(cnxParams.vmxSpec, vmxSpec.CharPtr());

            CharArWrapper serverName(appGlobals.host);
            cnxParams.serverName = new char[serverName.size()];
            strcpy(cnxParams.serverName, serverName.CharPtr());

            if (appGlobals.cookie == "") {
                cnxParams.credType = VIXDISKLIB_CRED_UID;
                CharArWrapper password(appGlobals.password);
                cnxParams.creds.uid.password = new char[password.size()];
                strcpy(cnxParams.creds.uid.password, password.CharPtr());
                CharArWrapper password(appGlobals.password);
                cnxParams.creds.uid.password = new char[password.size()];
                strcpy(cnxParams.creds.uid.password, password.CharPtr());
                      } else {
                        cnxParams.credType = VIXDISKLIB_CRED_SESSIONID;
                        cnxParams.creds.sessionId.cookie = appGlobals.cookie;
                        cnxParams.creds.sessionId.userName = appGlobals.userName;
                        cnxParams.creds.sessionId.key = appGlobals.password;
                      }

//            cnxParams.credType = VIXDISKLIB_CRED_UID;

//            CharArWrapper userName(appGlobals.userName);
//            cnxParams.creds.uid.userName = new char[userName.size()];
//            strcpy(cnxParams.creds.uid.userName, userName.CharPtr());

//            CharArWrapper password(appGlobals.password);
//            cnxParams.creds.uid.password = new char[password.size()];
//            strcpy(cnxParams.creds.uid.password, password.CharPtr());

            CharArWrapper thumbPrint(appGlobals.thumbPrint);
            cnxParams.thumbPrint = new char[thumbPrint.size()];
            strcpy(cnxParams.thumbPrint, thumbPrint.CharPtr());

            cnxParams.port = appGlobals.port;
            cnxParams.nfcHostPort = appGlobals.nfcHostPort;
        }

        CharArWrapper lib(appGlobals.libdir);
        CharArWrapper cfg(appGlobals.cfgFile);

        if (appGlobals.useInitEx) {
            vixError = VixDiskLib_InitEx(VIXDISKLIB_VERSION_MAJOR,
                                         VIXDISKLIB_VERSION_MINOR,
                                         &LogFunc, &WarnFunc, &PanicFunc,
                                         lib.CharPtr(),
                                         cfg.CharPtr());

        } else {
            vixError = VixDiskLib_Init(VIXDISKLIB_VERSION_MAJOR,
                                       VIXDISKLIB_VERSION_MINOR,
                                       NULL, NULL, NULL, // Log, warn, panic
                                       lib.CharPtr());
        }
        CHECK_AND_THROW(vixError);
        bVixInit = true;

        QString exeName("VixDiskLib_PrepareForAccess() started by " +
                        QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath());

        CharArWrapper exe(exeName);
        CharArWrapper ssMoRef(appGlobals.ssMoRef);
        CharArWrapper trModes(appGlobals.transportModes);

        if (appGlobals.vmxSpec != "") {
            vixError = VixDiskLib_PrepareForAccess(&cnxParams, exe.CharPtr());
            CHECK_AND_THROW(vixError);
        }
        if (appGlobals.ssMoRef == "" && appGlobals.transportModes == "") {
            vixError = VixDiskLib_Connect(&cnxParams,
                                          &appGlobals.connection);
        } else {
            Bool ro = (appGlobals.openFlags & VIXDISKLIB_FLAG_OPEN_READ_ONLY);
            vixError = VixDiskLib_ConnectEx(&cnxParams, ro, ssMoRef.CharPtr(),
                                            trModes.CharPtr(), &appGlobals.connection);
        }
    } catch (const VixDiskLibErrWrapper& e) {
        cout << "Error: [" << e.File() << ":" << e.Line() << "]  " <<
                std::hex << e.ErrorCode() << " " << e.Description() << "\n";
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * DoCreate --
 *
 *      Creates a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoCreate()
{
    DoInit();

    VixDiskLibCreateParams createParams;
    VixError vixError;

    createParams.adapterType = appGlobals.adapterType;

    createParams.capacity = appGlobals.mbSize * 2048;
    createParams.diskType = VIXDISKLIB_DISK_MONOLITHIC_SPARSE;
    createParams.hwVersion = VIXDISKLIB_HWVERSION_WORKSTATION_5;

    vixError = VixDiskLib_Create(appGlobals.connection,
                                 appGlobals.diskPath.toUtf8().constData(),
                                 &createParams,
                                 NULL,
                                 NULL);
    CHECK_AND_THROW(vixError);
}

/*
 *--------------------------------------------------------------------------
 *
 * DoRedo --
 *
 *      Creates a child disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoRedo()
{
    DoInit();

    VixError vixError;
    VixDisk parentDisk(appGlobals.connection, appGlobals.parentPath.toUtf8().data(), 0);
    vixError = VixDiskLib_CreateChild(parentDisk.Handle(),
                                      appGlobals.diskPath.toUtf8().constData(),
                                      VIXDISKLIB_DISK_MONOLITHIC_SPARSE,
                                      NULL, NULL);
    CHECK_AND_THROW(vixError);
}

/*
 *--------------------------------------------------------------------------
 *
 * DoFill --
 *
 *      Writes to a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoFill()
{
    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    uint8 buf[VIXDISKLIB_SECTOR_SIZE];
    VixDiskLibSectorType startSector;

    memset(buf, appGlobals.filler, sizeof buf);

    for (startSector = 0; startSector < appGlobals.numSectors; ++startSector) {
       VixError vixError;
       vixError = VixDiskLib_Write(disk.Handle(),
                                   appGlobals.startSector + startSector,
                                   1, buf);
       CHECK_AND_THROW(vixError);
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * DoDump --
 *
 *      Dumps the content of a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoDump()
{
    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    uint8 buf[VIXDISKLIB_SECTOR_SIZE];
    VixDiskLibSectorType i;

    for (i = 0; i < appGlobals.numSectors; i++) {
        VixError vixError = VixDiskLib_Read(disk.Handle(),
                                            appGlobals.startSector + i,
                                            1, buf);
        CHECK_AND_THROW(vixError);
        DumpBytes(buf, sizeof buf, 16);
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * DoReadMetadata --
 *
 *      Reads metadata from a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoReadMetadata()
{
    DoInit();

    size_t requiredLen;
    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    VixError vixError = VixDiskLib_ReadMetadata(disk.Handle(),
                                                appGlobals.metaKey.toUtf8().constData(),
                                                NULL, 0, &requiredLen);
    if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
        THROW_ERROR(vixError);
    }
    std::vector <char> val(requiredLen);
    vixError = VixDiskLib_ReadMetadata(disk.Handle(),
                                       appGlobals.metaKey.toUtf8().constData(),
                                       &val[0],
                                       requiredLen,
                                       NULL);
    CHECK_AND_THROW(vixError);
    cout << appGlobals.metaKey.toUtf8().constData() << " = " << &val[0] << endl;
}

/*
 *--------------------------------------------------------------------------
 *
 * DoWriteMetadata --
 *
 *      Writes metadata in a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoWriteMetadata()
{
    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    VixError vixError = VixDiskLib_WriteMetadata(disk.Handle(),
                                                 appGlobals.metaKey.toUtf8().constData(),
                                                 appGlobals.metaVal.toUtf8().constData());
    CHECK_AND_THROW(vixError);
}

/*
 *--------------------------------------------------------------------------
 *
 * DoDumpMetadata --
 *
 *      Dumps all the metadata.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoDumpMetadata()
{
    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    char *key;
    size_t requiredLen;

    VixError vixError = VixDiskLib_GetMetadataKeys(disk.Handle(),
                                                   NULL, 0, &requiredLen);
    if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
       THROW_ERROR(vixError);
    }
    std::vector<char> buf(requiredLen);
    vixError = VixDiskLib_GetMetadataKeys(disk.Handle(), &buf[0], requiredLen, NULL);
    CHECK_AND_THROW(vixError);
    key = &buf[0];

    while (*key) {
        vixError = VixDiskLib_ReadMetadata(disk.Handle(), key, NULL, 0,
                                           &requiredLen);
        if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
           THROW_ERROR(vixError);
        }
        std::vector <char> val(requiredLen);
        vixError = VixDiskLib_ReadMetadata(disk.Handle(), key, &val[0],
                                           requiredLen, NULL);
        CHECK_AND_THROW(vixError);
        cout << key << " = " << &val[0] << endl;
        key += (1 + strlen(key));
    }
}

/*
 *--------------------------------------------------------------------------
 *
 * DoInfo --
 *
 *      Queries the information of a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::DoInfo()
{
    appGlobals.isRemote = true;

    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    VixDiskLibInfo *info = NULL;
    VixError vixError;

    vixError = VixDiskLib_GetInfo(disk.Handle(), &info);

    CHECK_AND_THROW(vixError);

    emit signalStdOut("capacity          = "); //info->capacity); << " sectors" << endl;
    //cout << "number of links   = " << info->numLinks << endl;
    //cout << "adapter type      = ";

    cout << "capacity          = " << info->capacity << " sectors" << endl;
    cout << "number of links   = " << info->numLinks << endl;
    cout << "adapter type      = ";
    switch (info->adapterType) {
    case VIXDISKLIB_ADAPTER_IDE:
       cout << "IDE" << endl;
       break;
    case VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC:
       cout << "BusLogic SCSI" << endl;
       break;
    case VIXDISKLIB_ADAPTER_SCSI_LSILOGIC:
       cout << "LsiLogic SCSI" << endl;
       break;
    default:
       cout << "unknown" << endl;
       break;
    }

    cout << "BIOS geometry     = " << info->biosGeo.cylinders <<
       "/" << info->biosGeo.heads << "/" << info->biosGeo.sectors << endl;

    cout << "physical geometry = " << info->physGeo.cylinders <<
       "/" << info->physGeo.heads << "/" << info->physGeo.sectors << endl;

    VixDiskLib_FreeInfo(info);

    cout << "Transport modes supported by vixDiskLib: " <<
            VixDiskLib_ListTransportModes() << endl;
}

/*
 *----------------------------------------------------------------------
 *
 * DoTestMultiThread --
 *
 *      Starts a given number of threads, each of which will copy the
 *      source disk to a temp. file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::DoTestMultiThread()
{
    DoInit();

    VixDiskLibConnectParams cnxParams = { 0 };
    VixDiskLibConnection dstConnection;
    VixError vixError;
    vector<ThreadData> threadData(appGlobals.numThreads);
    int i;

    vixError = VixDiskLib_Connect(&cnxParams, &dstConnection);
    CHECK_AND_THROW(vixError);

 #ifdef _WIN32
    vector<HANDLE> threads(appGlobals.numThreads);

    for (i = 0; i < appGlobals.numThreads; i++) {
       unsigned int threadId;

       PrepareThreadData(dstConnection, threadData[i]);
       threads[i] = (HANDLE)_beginthreadex(NULL, 0, &CopyThread,
                                           (void*)&threadData[i], 0, &threadId);
    }
    WaitForMultipleObjects(appGlobals.numThreads, &threads[0], TRUE, INFINITE);
 #else
    vector<pthread_t> threads(appGlobals.numThreads);

    for (i = 0; i < appGlobals.numThreads; i++) {
       PrepareThreadData(dstConnection, threadData[i]);
       pthread_create(&threads[i], NULL, &CopyThread, (void*)&threadData[i]);
    }
    for (i = 0; i < appGlobals.numThreads; i++) {
       void *hlp;
       pthread_join(threads[i], &hlp);
    }
 #endif

    for (i = 0; i < appGlobals.numThreads; i++) {
       VixDiskLib_Close(threadData[i].srcHandle);
       VixDiskLib_Close(threadData[i].dstHandle);
       VixDiskLib_Unlink(dstConnection, threadData[i].dstDisk.c_str());
    }
    VixDiskLib_Disconnect(dstConnection);
    if (!appGlobals.success) {
       THROW_ERROR(VIX_E_FAIL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DoClone --
 *
 *      Clones a local disk (possibly to an ESX host).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::DoClone()
{
    DoInit();

    VixDiskLibConnection srcConnection;
    VixDiskLibConnectParams cnxParams = { 0 };
    VixError vixError = VixDiskLib_Connect(&cnxParams, &srcConnection);
    CHECK_AND_THROW(vixError);

    /*
     *  Note : These createParams are ignored for remote case
     */

    VixDiskLibCreateParams createParams;
    createParams.adapterType = appGlobals.adapterType;
    createParams.capacity = appGlobals.mbSize * 2048;
    createParams.diskType = VIXDISKLIB_DISK_MONOLITHIC_SPARSE;
    createParams.hwVersion = VIXDISKLIB_HWVERSION_WORKSTATION_5;

    vixError = VixDiskLib_Clone(appGlobals.connection,
                                appGlobals.diskPath.toUtf8().constData(),
                                srcConnection,
                                appGlobals.srcPath.toUtf8().constData(),
                                &createParams,
                                CloneProgressFunc,
                                NULL,   // clientData
                                TRUE);  // doOverWrite
    VixDiskLib_Disconnect(srcConnection);
    CHECK_AND_THROW(vixError);
    cout << "\n Done" << "\n";
}

/*
 *--------------------------------------------------------------------------
 *
 * BitCount --
 *
 *      Counts all the bits set in an int.
 *
 * Results:
 *      Number of bits set to 1.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

int worker::BitCount(int number)
{
    int bits = 0;
    while (number) {
        number = number & (number - 1);
        bits++;
    }
    return bits;
}

void worker::run()
{
    m_thread->start();

    switch (appGlobals.command) {
    case COMMAND_CREATE:
        DoCreate();
        break;
    case COMMAND_DUMP:
        DoDump();
        break;
    case COMMAND_FILL:
        DoFill();
        break;
    case COMMAND_INFO:
        DoInfo();
        break;
    case COMMAND_REDO:
        DoRedo();
        break;
    case COMMAND_DUMP_META:
        DoReadMetadata();
        break;
    case COMMAND_READ_META:
        DoReadMetadata();
        break;
    case COMMAND_WRITE_META:
        DoWriteMetadata();
        break;
    case COMMAND_MULTITHREAD:
        DoTestMultiThread();
        break;
    case COMMAND_CLONE:
        DoClone();
        break;
    case COMMAND_READBENCH:
        DoRWBench(true);
        break;
    case COMMAND_WRITEBENCH:
        DoRWBench(false);
        break;
    case COMMAND_CHECKREPAIR:
        if (appGlobals.repair)
            DoCheckRepair(true);
        else
            DoCheckRepair(false);
        break;
    case COMMAND_SHRINK:
        //TBD
        break;
    case COMMAND_DEFRAG:
        //TBD
        break;
    }

    m_thread->quit();
}

/*
 *----------------------------------------------------------------------
 *
 * DumpBytes --
 *
 *      Displays an array of n bytes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::DumpBytes(const uint8 *buf, size_t n, int step)
{
    size_t lines = n / step;
    size_t i;

    for (i = 0; i < lines; i++) {
       int k, last;
       printf("%04x : ", i * step);
       for (k = 0; n != 0 && k < step; k++, n--) {
          printf("%02x ", buf[i * step + k]);
       }
       printf("  ");
       last = k;
       while (k --) {
          unsigned char c = buf[i * step + last - k - 1];
          if (c < ' ' || c >= 127) {
             c = '.';
          }
          printf("%c", c);
       }
       printf("\n");
    }
    printf("\n");
}

/*
 *----------------------------------------------------------------------
 *
 * DoRWBench --
 *
 *      Perform read/write benchmarks according to settings in
 *      appGlobals. Note that a write benchmark will destroy the data
 *      in the target disk.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::DoRWBench(bool read)
{
    DoInit();

    VixDisk disk(appGlobals.connection, appGlobals.diskPath.toUtf8().data(), appGlobals.openFlags);
    size_t bufSize;
    uint8 *buf;
    VixDiskLibInfo *info;
    VixError err;
    uint32 maxOps, i;
    uint32 bufUpdate;
    struct timeval start, end, total;

    if (appGlobals.bufSize == 0) {
       appGlobals.bufSize = DEFAULT_BUFSIZE;
    }
    bufSize = appGlobals.bufSize * VIXDISKLIB_SECTOR_SIZE;

    buf = new uint8[bufSize];
    if (!read) {
       InitBuffer((uint32*)buf, bufSize / sizeof(uint32));
    }

    err = VixDiskLib_GetInfo(disk.Handle(), &info);
    if (VIX_FAILED(err)) {
       delete [] buf;
       throw VixDiskLibErrWrapper(err, __FILE__, __LINE__);
    }

    maxOps = info->capacity / appGlobals.bufSize;
    VixDiskLib_FreeInfo(info);

    printf("Processing %d buffers of %d bytes.\n", maxOps, (uint32)bufSize);

    gettimeofday(&total, NULL);
    start = total;
    bufUpdate = 0;
    for (i = 0; i < maxOps; i++) {
       VixError vixError;

       if (read) {
          vixError = VixDiskLib_Read(disk.Handle(),
                                     i * appGlobals.bufSize,
                                     appGlobals.bufSize, buf);
       } else {
          vixError = VixDiskLib_Write(disk.Handle(),
                                      i * appGlobals.bufSize,
                                      appGlobals.bufSize, buf);

       }
       if (VIX_FAILED(vixError)) {
          delete [] buf;
          throw VixDiskLibErrWrapper(vixError, __FILE__, __LINE__);
       }

       bufUpdate += appGlobals.bufSize;
       if (bufUpdate >= BUFS_PER_STAT) {
          gettimeofday(&end, NULL);
          PrintStat(read, start, end, bufUpdate);
          start = end;
          bufUpdate = 0;
       }
    }
    gettimeofday(&end, NULL);
    PrintStat(read, total, end, appGlobals.bufSize * maxOps);
    delete [] buf;
}

/*
 *----------------------------------------------------------------------
 *
 * DoCheckRepair --
 *
 *      Check a sparse disk for internal consistency.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::DoCheckRepair(Bool repair)
{
    DoInit();

    VixError err;

    err = VixDiskLib_CheckRepair(appGlobals.connection, appGlobals.diskPath.toUtf8().data(),
                                 repair);
    if (VIX_FAILED(err)) {
       throw VixDiskLibErrWrapper(err, __FILE__, __LINE__);
    }
}

//void worker::DoAsyncIO(bool read)
//{
//    std::vector<VixDisk::Ptr> disks(appGlobals.diskPaths.size());
//    TaskExecutor tasks(appGlobals.diskPaths.size());
//    for (int i = 0 ; i < appGlobals.diskPaths.size() ; ++i) {
//       VixDisk::Ptr disk = boost::make_shared<VixDisk>(appGlobals.connection,
//                    appGlobals.diskPaths[i].c_str(),
//                    appGlobals.openFlags, i);
//       disks[i] = disk;
//       tasks.addTask(boost::bind(&VixDiskAsyncReadWrite, disk, read));
//    }
//}

/*
 *--------------------------------------------------------------------------
 *
 * PrintUsage --
 *
 *      Displays the usage message.
 *
 * Results:
 *      1.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

int worker::PrintUsage()
{
    printf("Usage: vixdisklibsample.exe command [options] diskPath\n\n");

    printf("List of commands (all commands are mutually exclusive):\n");
    printf(" -create : creates a sparse virtual disk with capacity "
           "specified by -cap\n");
    printf(" -redo parentPath : creates a redo log 'diskPath' "
           "for base disk 'parentPath'\n");
    printf(" -info : displays information for specified virtual disk\n");
    printf(" -dump : dumps the contents of specified range of sectors "
           "in hexadecimal\n");
    printf(" -fill : fills specified range of sectors with byte value "
           "specified by -val\n");
    printf(" -wmeta key value : writes (key,value) entry into disk's metadata table\n");
    printf(" -rmeta key : displays the value of the specified metada entry\n");
    printf(" -meta : dumps all entries of the disk's metadata\n");
    printf(" -clone sourcePath : clone source vmdk possibly to a remote site\n");
    printf(" -readbench blocksize: Does a read benchmark on a disk using the \n");
    printf("specified I/O block size (in sectors).\n");
    printf(" -writebench blocksize: Does a write benchmark on a disk using the\n");
    printf("specified I/O block size (in sectors). WARNING: This will\n");
    printf("overwrite the contents of the disk specified.\n");
    printf(" -check repair: Check a sparse disk for internal consistency, "
           "where repair is a boolean value to indicate if a repair operation "
           "should be attempted.\n\n");

    printf("options:\n");
    printf(" -adapter [ide|scsi] : bus adapter type for 'create' option "
           "(default='scsi')\n");
    printf(" -start n : start sector for 'dump/fill' options (default=0)\n");
    printf(" -count n : number of sectors for 'dump/fill' options (default=1)\n");
    printf(" -val byte : byte value to fill with for 'write' option (default=255)\n");
    printf(" -cap megabytes : capacity in MB for -create option (default=100)\n");
    printf(" -single : open file as single disk link (default=open entire chain)\n");
    printf(" -multithread n: start n threads and copy the file to n new files\n");
    printf(" -host hostname : hostname/IP address of VC/vSphere host (Mandatory)\n");
    printf(" -user userid : user name on host (Mandatory) \n");
    printf(" -password password : password on host. (Mandatory)\n");
    printf(" -port port : port to use to connect to VC/ESXi host (default = 443) \n");
    printf(" -nfchostport port : port to use to establish NFC connection to ESXi host (default = 902) \n");
    printf(" -vm moref=id : id is the managed object reference of the VM \n");
    printf(" -libdir dir : Folder location of the VDDK installation. "
           "On Windows, the bin folder holds the plugin.  On Linux, it is "
           "the lib64 directory\n");
    printf(" -initex configfile : Specify path and filename of config file \n");
    printf(" -ssmoref moref : Managed object reference of VM snapshot \n");
    printf(" -mode mode : Mode string to pass into VixDiskLib_ConnectEx. "
            "Valid modes are: nbd, nbdssl, san, hotadd \n");
    printf(" -thumb string : Provides a SSL thumbprint string for validation. "
           "Format: xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx\n");

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateRandomFilename --
 *
 *      Generate and return a random filename.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::GenerateRandomFilename(const std::string &prefix, std::string &randomFilename)
{
    string retStr;
    int strLen = sizeof(randChars) - 1;

    for (unsigned int i = 0; i < 8; i++)
    {
        retStr += randChars[rand() % strLen];
    }
    randomFilename = prefix + retStr;
}

/*
 *----------------------------------------------------------------------
 *
 * gettimeofday --
 *
 *      Mimics BSD style gettimeofday in a way that is close enough
 *      for some I/O benchmarking.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void worker::gettimeofday(timeval *tv, void *)
{
    uint64 ticks = GetTickCount();

    tv->tv_sec = ticks / 1000;
    tv->tv_usec = 1000 * (ticks % 1000);
}

/*
 *--------------------------------------------------------------------------
 *
 * LogFunc --
 *
 *      Callback for VixDiskLib Log messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::LogFunc(const char *fmt, va_list args)
{
    printf("Log: ");
    vprintf(fmt, args);
}

/*
 *--------------------------------------------------------------------------
 *
 * WarnFunc --
 *
 *      Callback for VixDiskLib Warning messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::WarnFunc(const char *fmt, va_list args)
{
    printf("Warning: ");
    vprintf(fmt, args);
}

/*
 *--------------------------------------------------------------------------
 *
 * PanicFunc --
 *
 *      Callback for VixDiskLib Panic messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

void worker::PanicFunc(const char *fmt, va_list args)
{
    printf("Panic: ");
    vprintf(fmt, args);
    //exit(10);
}

/*
 *----------------------------------------------------------------------
 *
 * CloneProgress --
 *
 *      Callback for the clone function.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool worker::CloneProgressFunc(void *, int percentCompleted)
{
    cout << "Cloning : " << percentCompleted << "% Done" << "\r";
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * CopyThread --
 *
 *       Copies a source disk to the given file.
 *
 * Results:
 *       0 if succeeded, 1 if not.
 *
 * Side effects:
 *      Creates a new disk; sets appGlobals.success to false if fails
 *
 *----------------------------------------------------------------------
 */

unsigned worker::CopyThread(void *arg)
{
    ThreadData *td = (ThreadData *)arg;

    try {
        VixDiskLibSectorType i;
        VixError vixError;
        uint8 buf[VIXDISKLIB_SECTOR_SIZE];

        for (i = 0; i < td->numSectors; i ++) {
            vixError = VixDiskLib_Read(td->srcHandle, i, 1, buf);
            CHECK_AND_THROW(vixError);
            vixError = VixDiskLib_Write(td->dstHandle, i, 1, buf);
            CHECK_AND_THROW(vixError);
        }

    } catch (const VixDiskLibErrWrapper& e) {
        cout << "CopyThread (" << td->dstDisk << ")Error: " << e.ErrorCode()
             <<" " << e.Description();
        appGlobals.success = FALSE;
        return TASK_FAIL;
    }

    cout << "CopyThread to " << td->dstDisk << " succeeded.\n";
    return TASK_OK;
}

