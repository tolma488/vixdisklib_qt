#ifndef WORKER_H
#define WORKER_H

#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/make_shared.hpp>

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <process.h>
#else
#include <dlfcn.h>
#include <sys/time.h>
#endif

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

#include "vixDiskLib.h"

#include <QObject>
#include <QThread>

using std::cout;
using std::string;
using std::endl;
using std::vector;


#define COMMAND_CREATE              (1 << 0)
#define COMMAND_DUMP                (1 << 1)
#define COMMAND_FILL                (1 << 2)
#define COMMAND_INFO                (1 << 3)
#define COMMAND_REDO                (1 << 4)
#define COMMAND_DUMP_META           (1 << 5)
#define COMMAND_READ_META           (1 << 6)
#define COMMAND_WRITE_META          (1 << 7)
#define COMMAND_MULTITHREAD         (1 << 8)
#define COMMAND_CLONE               (1 << 9)
#define COMMAND_READBENCH           (1 << 10)
#define COMMAND_WRITEBENCH          (1 << 11)
#define COMMAND_CHECKREPAIR         (1 << 12)
#define COMMAND_SHRINK              (1 << 13)
#define COMMAND_DEFRAG              (1 << 14)
#define COMMAND_READASYNCBENCH      (1 << 15)
#define COMMAND_WRITEASYNCBENCH     (1 << 16)

#define VIXDISKLIB_VERSION_MAJOR 6
#define VIXDISKLIB_VERSION_MINOR 5

#define TASK_OK 0
#define TASK_FAIL 1

// Default buffer size (in sectors) for read/write benchmarks
#define DEFAULT_BUFSIZE 128

// Print updated statistics for read/write benchmarks roughly every
// BUFS_PER_STAT sectors (current value is 64MBytes worth of data)
#define BUFS_PER_STAT (128 * 1024)

// Character array for random filename generation
static const char randChars[] = "0123456789"
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// Per-thread information for multi-threaded VixDiskLib test.
struct ThreadData {
   std::string dstDisk;
   VixDiskLibHandle srcHandle;
   VixDiskLibHandle dstHandle;
   VixDiskLibSectorType numSectors;
};


#define THROW_ERROR(vixError) \
   throw VixDiskLibErrWrapper((vixError), __FILE__, __LINE__)

//don't throw errors if vixError == VIX_E_NOT_SUPPORTED_ON_REMOTE_OBJECT in case of standalone ESXi hosts

#define CHECK_AND_THROW_2(vixError, buf)															\
   do {																								\
      if (VIX_FAILED((vixError)  && vixError != 20)) {												\
         delete [] buf;																				\
         throw VixDiskLibErrWrapper((vixError), __FILE__, __LINE__);								\
      }	else if (vixError == VIX_E_NOT_SUPPORTED_ON_REMOTE_OBJECT) {								\
                std::cout << (VixDiskLibErrWrapper((vixError), __FILE__, __LINE__)).Description()	\
                          << std::endl;																\
                delete [] buf;																		\
      }																								\
   } while (0)

#define CHECK_AND_THROW(vixError) CHECK_AND_THROW_2(vixError, ((int*)0))

typedef void (VixDiskLibGenericLogFunc)(const char *fmt, va_list args);

enum class bMode {NOT_SET, NBD, NBDSSL, HOTADD, SAN};        //backup mode

struct WorkerConfig
{
    int command;
    VixDiskLibAdapterType adapterType;
    QString transportModes;
    QString diskPath;
    QString parentPath;
    QString metaKey;
    QString metaVal;
    int filler;
    unsigned mbSize;
    VixDiskLibSectorType numSectors;
    VixDiskLibSectorType startSector;
    VixDiskLibSectorType bufSize;
    uint32 openFlags;
    unsigned numThreads;
    bool success;
    bool isRemote;
    QString host;
    QString userName;
    QString password;
    QString cookie;
    QString thumbPrint;
    QString vimApiVer;
    int port;
    int nfcHostPort;
    QString srcPath;
    VixDiskLibConnection connection;
    QString vmxSpec;
    bool useInitEx;
    QString cfgFile;
    QString libdir;
    QString ssMoRef;
    int repair;

    int blockSize;
    bMode backupMode;
    QString tmpDir;
    QString logFile;
    unsigned logLevel;
    unsigned nfcLogLevel;
    bool caching;
    bool cmdOnly;
};

struct CharArWrapper                                                    //wrapper class to return char* from QString
{
    QByteArray text;
    CharArWrapper();
    CharArWrapper(QString &s): text(s.toUtf8()){}
    const char* CharPtr()const { return text.data(); }
    int size() { return text.size(); }
};

class worker : public QThread
{    
    Q_OBJECT

    QThread *m_thread;

    static WorkerConfig appGlobals;
    static VixDiskLibConnectParams cnxParams;                           //Connection setup parameters
    static bool bVixInit;
    friend class vixdisklibsamplegui;
    static void InitBuffer(uint32 *buf, uint32 numElems);               //Fill an array of uint32 with random values, to defeat any attempts to compress it.

    static void PrepareThreadData(VixDiskLibConnection &dstConnection,  //Open the source and destination disk for multi threaded copy.
                                  ThreadData &td);
    static void PrintStat(bool read, struct timeval start,              //Print performance statistics for read/write benchmarks.
                          struct timeval end, uint32 numSectors);
    static void GenerateRandomFilename(const string& prefix,            //Generate and return a random filename.
                                       string& randomFilename);
    static void gettimeofday(struct timeval *tv, void *);               //Mimics BSD style gettimeofday in a way that is close enough for some I/O benchmarking.
    static void LogFunc(const char *fmt, va_list args);                 //Callback for VixDiskLib Log messages.
    static void WarnFunc(const char *fmt, va_list args);                //Callback for VixDiskLib Warning messages.
    static void PanicFunc(const char *fmt, va_list args);               //Callback for VixDiskLib Panic messages.
    static Bool CloneProgressFunc(void * /*progressData*/,
                                  int percentCompleted);
    static unsigned __stdcall CopyThread(void *arg);                    //Copies a source disk to the given file.
    int BitCount(int number);                                           //Counts all the bits set in an int.

protected:
    virtual void run();

public:

    worker();
    ~worker();
    int ParseArguments(int argc, char* argv[]);                  //Parses the arguments passed on the command line.
    void DoInit(void);                                             //Initializes vixdisklib
    void DoCreate(void);                                         //Creates a virtual disk.
    void DoRedo(void);                                           //Creates a child disk.
    void DoFill(void);                                           //Writes to a virtual disk.
    void DoDump(void);                                           //Dumps the content of a virtual disk.
    void DoReadMetadata(void);                                   //Reads metadata from a virtual disk.
    void DoWriteMetadata(void);                                  //Writes metadata in a virtual disk.
    void DoDumpMetadata(void);                                   //Dumps all the metadata.
    void DoInfo(void);                                           //Queries the information of a virtual disk.
    void DoTestMultiThread(void);                                //Starts a given number of threads, each of which will copy the source disk to a temp. file.
    void DoClone(void);                                          //Clones a local disk (possibly to an ESX host).
    void DumpBytes(const uint8 *buf, size_t n, int step);        //Displays an array of n bytes.
    void DoRWBench(bool read);                                   //Perform read/write benchmarks
    void DoCheckRepair(Bool repair);                             //Check a sparse disk for internal consistency.
    //void DoAsyncIO(bool read);                                   //?????????
    //helper methods

    int PrintUsage(void);                                        //Displays the usage message.

signals:
    void signalStdOut(QString text);
};

// Wrapper class for VixDiskLib disk objects.

class VixDiskLibErrWrapper
{
public:
    explicit VixDiskLibErrWrapper(VixError errCode, const char* file, int line)
          :
          _errCode(errCode),
          _file(file),
          _line(line)
    {
        char* msg = VixDiskLib_GetErrorText(errCode, NULL);
        _desc = msg;
        VixDiskLib_FreeErrorText(msg);
    }

    VixDiskLibErrWrapper(const char* description, const char* file, int line)
          :
         _errCode(VIX_E_FAIL),
         _desc(description),
         _file(file),
         _line(line)
    {
    }

    string Description() const { return _desc; }
    VixError ErrorCode() const { return _errCode; }
    string File() const { return _file; }
    int Line() const { return _line; }

private:
    VixError _errCode;
    string _desc;
    string _file;
    int _line;
};

class VixDisk
{
public:
    typedef boost::shared_ptr<VixDisk> Ptr;

    VixDiskLibHandle Handle() const { return _handle; }
    VixDisk(VixDiskLibConnection connection, const char *path, uint32 flags, int id = 0)
       : _id(id)
    {
       _handle = NULL;
       VixError vixError = VixDiskLib_Open(connection, path, flags, &_handle);
       CHECK_AND_THROW(vixError);
       printf("Disk[%d] \"%s\" is opened using transport mode \"%s\".\n",
              id, path, VixDiskLib_GetTransportMode(_handle));

       vixError = VixDiskLib_GetInfo(_handle, &_info);
       CHECK_AND_THROW(vixError);
    }

    int getId() const
    {
       return _id;
    }

    const VixDiskLibInfo* getInfo() const
    {
       return _info;
    }

    ~VixDisk()
    {
        if (_handle) {
           VixDiskLib_FreeInfo(_info);
           VixDiskLib_Close(_handle);
           printf("Disk[%d] is closed.\n", _id);
        }
        _info = NULL;
        _handle = NULL;
    }

private:
    VixDiskLibHandle _handle;
    VixDiskLibInfo *_info;
    int _id;
};


template <bool C, typename T, typename F>
struct IF_THEN_ELSE;

template <typename T, typename F>
struct IF_THEN_ELSE<true, T, F>
{
   typedef T result;
};

template <typename T, typename F>
struct IF_THEN_ELSE<false, T, F>
{
   typedef F result;
};

template <int N>
struct INT_TYPE
{
   enum {value = N};
};

template <typename T1, typename T2, typename N = INT_TYPE<1> >
struct sizeT
{
   enum
   {
      value = IF_THEN_ELSE<(sizeof(T1) > sizeof(T2[N::value])),
                           sizeT<T1, T2, INT_TYPE<N::value+1> >,
                           N>::result::value
   };
};

class ThreadLock
{
   public:
      ThreadLock()
      {
#ifdef _WIN32
         InitializeCriticalSection(&cs);
         //InitializeConditionVariable(&cond);
#else
         pthread_mutex_init(&mutex, NULL);
         pthread_cond_init(&cond, NULL);
#endif
      }
      ~ThreadLock()
      {
#ifndef _WIN32
         pthread_mutex_destroy(&mutex);
         pthread_cond_destroy(&cond);
#endif
      }

      void lock()
      {
#ifdef _WIN32
         EnterCriticalSection(&cs);
#else
         pthread_mutex_lock(&mutex);
#endif
      }
      void unlock()
      {
#ifdef _WIN32
         LeaveCriticalSection(&cs);
#else
         pthread_mutex_unlock(&mutex);
#endif
      }
      bool wait()
      {
#ifdef _WIN32
         //SleepConditionVariableCS(&cond, &cs, INFINITE);
#else
         pthread_cond_wait(&cond, &mutex);
#endif
         return true;
      }
      void notify()
      {
#ifdef _WIN32
         //WakeConditionVariable(&cond);
#else
         pthread_cond_signal(&cond);
#endif
      }
   private:
      ThreadLock(const ThreadLock&);
      ThreadLock& operator = (const ThreadLock&);

#ifdef _WIN32
      CRITICAL_SECTION cs;
      CONDITION_VARIABLE cond;
#else
      pthread_mutex_t mutex;
      pthread_cond_t cond;
#endif
};

struct FakeLock
{
   void lock() {}
   void unlock() {}
   bool wait()
   {
      return false;
   }

   void notify() {}
};

template <typename LCK>
struct LockGuard
{
   explicit LockGuard(LCK& l)
      : lock(l)
   {
      lock.lock();
   }
   ~LockGuard()
   {
      lock.unlock();
   }
   private:
      LCK& lock;
};

template <size_t SIZE, typename TYPE = char, typename LOCK = FakeLock>
class BufferPool : private LOCK
{
   typedef std::list<TYPE*> Pool;
   typedef typename Pool::iterator PoolIt;
   public:
      typedef TYPE type;

      explicit BufferPool(size_t bufSize)
      {
         initPool(bufSize);
      }

#if __cplusplus > 199711L
      BufferPool(size_t bufSize, LOCK&& lock)
         : LOCK(std::forward<LOCK>(lock))
#else
      BufferPool(size_t bufSize, const LOCK& lock)
         : LOCK(lock)
#endif
      {
         initPool(bufSize);
      }

      ~BufferPool()
      {
         {
            LockGrd lg(*this);
            while (!outPool.empty()) {
               if (!LOCK::wait()) {
                  break;
               }
            }
         }
         std::for_each(inPool.begin(), inPool.end(), freeBuffer());
         std::for_each(outPool.begin(), outPool.end(), freeBuffer());
      }

      size_t size()
      {
         return SIZE;
      }

      TYPE * getBuffer()
      {
         TYPE * buf = NULL;
         {
            LockGrd lg(*this);
            while (inPool.empty()) {
               if (!LOCK::wait()) {
                  return buf;
               }
            }
            PoolIt it = inPool.begin();
            outPool.splice(outPool.end(), inPool, it);
            buf = &(*it)[sizeT<PoolIt, TYPE>::value];
         }
         return buf;
      }
      void returnBuffer(TYPE * buf)
      {
         PoolIt it =
            *(reinterpret_cast<PoolIt*>(buf-sizeT<PoolIt, TYPE>::value));
         {
            LockGrd lg(*this);
            inPool.splice(inPool.end(), outPool, it);
         }
         LOCK::notify();
      }
   private:

      void initPool(size_t bufSize)
      {
         for (int i = 0 ; i < SIZE ; ++i) {
            try {
               std::auto_ptr<TYPE> buf(new TYPE[bufSize + sizeT<PoolIt, TYPE>::value]);
               inPool.push_front(buf.get());
               PoolIt * it = reinterpret_cast<PoolIt*>(buf.get());
               *it = inPool.begin();
               buf.release();
            } catch (...) {
               std::for_each(inPool.begin(), inPool.end(), freeBuffer());
               throw;
            }
         }
      }

      struct freeBuffer
      {
         void operator () (TYPE* buf) const
         {
            delete[] buf;
         }
      };

      typedef LockGuard<LOCK> LockGrd;

      Pool inPool;
      Pool outPool;
};

// specialization for unlimited size buffer pool
template <typename TYPE, typename LOCK>
class BufferPool<-1, TYPE, LOCK>
{
   public:
      typedef TYPE type;

      explicit BufferPool(size_t bz)
         : bufSize(bz)
         {}

      size_t size()
      {
         return -1;
      }

      TYPE* getBuffer()
      {
         return new TYPE[bufSize];
      }
      void returnBuffer(TYPE* buf)
      {
         delete [] buf;
      }
   private:
      size_t bufSize;
};

#ifndef VIX_AIO_BUFPOOL_SIZE
#define VIX_AIO_BUFPOOL_SIZE 256
#endif

typedef BufferPool<VIX_AIO_BUFPOOL_SIZE, uint8, ThreadLock> AioBufferPool;

template <typename Pool>
class AioCBData
{
   public:
      AioCBData(typename Pool::type * b, Pool& pool)
         : buf(b), aioBufPool(pool)
      {}

      void returnBuffer()
      {
         aioBufPool.returnBuffer(buf);
      }
   private:
      typename Pool::type * buf;
      Pool& aioBufPool;
};

template <typename CB>
static void AioCB(void * cbData, VixError err)
{
   CB* pCB = static_cast<CB*>(cbData);
   if (pCB == NULL) return;

   pCB->returnBuffer();
   delete pCB;

#ifdef AIO_PERF_DEBUG
   static int count = 0;
   if (++count % 20 == 0)
   {
      cout << ".";
      cout.flush();
   }
#endif
}


class TaskExecutor
{
   public:
      explicit TaskExecutor(size_t parallel_size)
         : m_work(boost::make_shared<boost::asio::io_service::work>(boost::ref(m_ioService))),
           m_threads(parallel_size)
      {
         for (int i = 0 ; i < m_threads.size() ; ++i) {
            m_threads[i] =
               boost::make_shared<boost::thread>(
                        boost::bind(&boost::asio::io_service::run,
                                    &m_ioService));
         }
      }

      ~TaskExecutor()
      {
         m_work.reset();
         for (int i = 0 ; i < m_threads.size() ; ++i) {
            m_threads[i]->join();
         }
      }

      template <typename Task>
      void addTask(Task t) {
         m_ioService.post(t);
      }

   private:
      boost::asio::io_service m_ioService;
      boost::shared_ptr<boost::asio::io_service::work> m_work;
      std::vector<boost::shared_ptr<boost::thread> > m_threads;
};

#endif // WORKER_H

