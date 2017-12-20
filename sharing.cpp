//
// sharing.cpp
//
// Copyright (C) 2013 - 2016 jones@scss.tcd.ie
//

#include "stdafx.h"                             // pre-compiled headers
#include <iostream>                             // cout
#include <iomanip>                              // setprecision
#include "helper.h"                             //
#include "BST.h"
#include <queue>
#include <random>

using namespace std;                            // cout

#define K           1024                        //
#define GB          (K*K*K)                     //
#define NOPS        10000                       //
#define NSECONDS    2                           // run each test for NSECONDS
#define REUSEQ_SIZE 100000

// Enum constants
#define TRANSACTION 0
#define LOCK 1
#define MAXATTEMPT 8

#define COUNTER64                               // comment for 32 bit counter
//#define FALSESHARING                          // allocate counters in same cache line

#ifdef COUNTER64
#define VINT    UINT64                          //  64 bit counter
#else
#define VINT    UINT                            //  32 bit counter
#endif

#define ALIGNED_MALLOC(sz, align) _aligned_malloc(sz, align)

#ifdef FALSESHARING
#define GINDX(n)    (g+n)                       //
#else
#define GINDX(n)    (g+n*lineSz/sizeof(VINT))   //
#endif

int key_ranges[5] = { 16, 256, 4096, 65535, 1048576 };
int current_bound = 0;
BST* tree;


UINT64 tstart;                                  // start of test in ms
int sharing;                                    // % sharing
int lineSz;                                     // cache line size
int maxThread;                                  // max # of threads

THREADH *threadH;                               // thread handles
UINT64 *ops;                                    // for ops per thread

#if TREETYP == 3
UINT64 *aborts;                                 // for counting aborts
#endif

typedef struct {
    int sharing;                                // sharing
    int nt;                                     // # threads
    UINT64 rt;                                  // run time (ms)
    UINT64 ops;                                 // ops
    UINT64 incs;                                // should be equal ops
    UINT64 aborts;                              //
} Result;

Result *r;                                      // results
UINT indx;                                      // results index

volatile VINT *g;                               // NB: position of volatile

//
// test memory allocation [see lecture notes]
//
ALIGN(64) UINT64 cnt0;
ALIGN(64) UINT64 cnt1;
ALIGN(64) UINT64 cnt2;
UINT64 cnt3;                                    // NB: in Debug mode allocated in cache line occupied by cnt0

/*
	TREETYP
	Used to switch between TATAS lock(0), HLE (1), RTM (2).
*/


#define TREETYP	2							//set up tree type

#if TREETYP == 0
#define TREESTR "BST_LOCK"
#define BST_LOCK
#define ADDNODE(n)			addNodeTATASLock(n);
#define REMOVENODE(key)		removeNodeTATASLock(key);

#elif TREETYP == 1
#define TREESTR "BST__HLE"
#define BST_HLE
#define ADDNODE(n)			addNodeHLE(n);
#define REMOVENODE(key)		removeNodeHLE(key);

#elif TREETYP == 2
#define TREESTR "BST_RTM"
#define BST_RTM
#define ADDNODE(n)			addNodeRTM(n);
#define REMOVENODE(key)		removeNodeRTM(key);

#endif

#ifdef BST_LOCK
void addNodeTATASLock(Node* n) {
	tree->acquireLock();
	tree->insertNode(n);
	tree->releaseLock();
}

Node* removeNodeTATASLock(int key) {
	tree->acquireLock();
	Node* removed = tree->removeNode(key);
	tree->releaseLock();
	return removed;
}
#endif
#ifdef BST_HLE
void addNodeHLE(Node* n) {
	tree->acquireHLE();
	tree->insertNode(n);
	tree->releaseHLE();
}


Node* removeNodeHLE(int key) {
	tree->acquireHLE();
	Node* removed = tree->removeNode(key);
	tree->releaseHLE();
	return removed;
}
#endif
#ifdef BST_RTM
void addNodeRTM(Node* n) {
	int state = TRANSACTION;
	int attempt = 1; // number of attempts
	
	while (1) {
		UINT status = _XBEGIN_STARTED; 
		if (state == TRANSACTION) { 
			// execute transactionally
			status = _xbegin(); 
		} else { 
			// Execute non-transactional path.
			tree->acquireLock();
		}
		
		if (status == _XBEGIN_STARTED) { 
			if (state == TRANSACTION && tree->lock) {// if executing transactionally, add lock to readset so transaction will abort if lock obtained by another thread
				_xabort(0xA0); // abort immediately if lock already set
			}
			// Here is where we add the node
			tree->insertNode(n);

			if (state == TRANSACTION) { 
				// end transaction ...
				_xend(); 
			} else { 
				// Release lock
				tree->releaseLock();
			}
			break;
		}
		else { 
			// Transaction aborted
			if (tree->lock) { 
				do { 
					_mm_pause();
				} while (tree->lock);
			}
			else { 
				volatile UINT64 wait = attempt << 4; // initialise wait and delay by ...
				while (wait--); 
			}
			if (++attempt >= MAXATTEMPT) 
				state = LOCK; // execute non transactionally by obtaining lock
		}
	}
}

Node* removeNodeRTM(int key) {
	int state = TRANSACTION; // TRANSACTION = 0 LOCK = 1
	int attempt = 1; // number of attempts
	Node *removed = NULL;

	while (1) {
		UINT status = _XBEGIN_STARTED;
		if (state == TRANSACTION) {
			// execute transactionally
			status = _xbegin();
		}
		else {
			// Execute non-transactional path.
			tree->acquireLock();
		}

		if (status == _XBEGIN_STARTED) {
			if (state == TRANSACTION && tree->lock) {// if executing transactionally, add lock to readset so transaction will abort if lock obtained by another thread
				_xabort(0xA0); // abort immediately if lock already set
			}
			// Here is where we remove the node
			removed = tree->removeNode(key);

			if (state == TRANSACTION) {
				// end transaction ...
				_xend();
			}
			else {
				// Release lock
				tree->releaseLock();
			}
			break;
		}
		else {
			// Transaction aborted
			if (tree->lock) {
				do {
					_mm_pause();
				} while (tree->lock);
			}
			else {
				volatile UINT64 wait = attempt << 4; // initialise wait and delay by ...
				while (wait--);
			}
			if (++attempt >= MAXATTEMPT)
				state = LOCK; // execute non transactionally by obtaining lock
		}
	}
	return removed;
}
#endif

// This method of generating random key taken from https://stackoverflow.com/questions/5008804/generating-random-integer-from-a-range
int generateRandomKey() {
	std::random_device rd;     // only used once to initialise (seed) engine
	std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)
	std::uniform_int_distribution<int> uni(0, key_ranges[current_bound]); // guaranteed unbiased

	auto random_key = uni(rng);
	return random_key;
}

// This method should pre-fill the binary tree so that it is half full starting off.
void prefillBinaryTree(int min_value, int max_value) {
	tree = new BST();

	// Only want to make it half-full - restrict to half the number of possible values
	int num_nodes = (max_value - min_value) / 2;

	for (int i = 0; i < num_nodes; i++) {
		int key = generateRandomKey();
		Node* n = new Node(key);
		tree->insertNode(n);
	}
}

// ReuseQ is used to prevent unnecessary inhibition of parallelism from calls to Malloc.
queue<Node*> make_empty_reuseq() {
	queue<Node*> ReuseQueue;
	for (int i = 0; i < REUSEQ_SIZE; i++) {
		ReuseQueue.push(new Node(0));
	}
	return ReuseQueue;
}

//
// worker
//
WORKER worker(void *vthread)
{
    int thread = (int)((size_t) vthread);

    UINT64 n = 0;

    volatile VINT *gt = GINDX(thread);
    volatile VINT *gs = GINDX(maxThread);

	// Make our reuseQ for this worker
	queue<Node*> ReuseQueue = make_empty_reuseq();
	
    runThreadOnCPU(thread % ncpu);

    while (1) {
		for (int i = 0; i < NOPS; i++)
		{
			
			int random_key = generateRandomKey();
			int update = random_key % 2;

			if (update == 1) {
				Node *n_add;

				if (!ReuseQueue.empty()) {
					// Take a node off the reuseQ to enhance performance
					n_add = ReuseQueue.front();
					ReuseQueue.pop();
					// Overwrite with our key
					n_add->key = random_key;
				} else {
					// Have to use Malloc :(
					n_add = new Node(random_key);
				}
				// Don't care if this is successful - count the operation anyway
				ADDNODE(n_add);

			} else {
				// We are removing this node if it exists
				Node* n_remove = REMOVENODE(random_key);
				
				// Don't care if this is successful - count the operation anyway
				if (n_remove != NULL) {
					// Add to reuseQ
					n_remove->left = NULL;
					n_remove->right = NULL;
					ReuseQueue.push(n_remove);
				}
			}
		}
		//record the number of operations performed
		n += NOPS;

		//break out of while loop if runtime exceeded
		if ((getWallClockMS() - tstart) > NSECONDS * 1000)
		{
			break;
		}
    }

    ops[thread] = n;

#if TREETYP == 3
    aborts[thread] = nabort;
#endif
	return 0;
}

//
// main
//
int main()
{
    ncpu = getNumberOfCPUs();   // number of logical CPUs
    maxThread = 2 * ncpu;       // max number of threads

	// Check whether HLE and RTM supported
	if (!rtmSupported()) {
		cout << "RTM is not supported!" << endl;
	}
	if (!hleSupported()) {
		cout << "HLE is not supported!" << endl;
	}

    //
    // get date
    //
    char dateAndTime[256];
    getDateAndTime(dateAndTime, sizeof(dateAndTime));

    //
    // console output
    //
    cout << getHostName() << " " << getOSName() << " sharing " << (is64bitExe() ? "(64" : "(32") << "bit EXE)" ;
#ifdef _DEBUG
    cout << " DEBUG";
#else
    cout << " RELEASE";
#endif
    cout << " [" << TREESTR << "]" << " NCPUS=" << ncpu << " RAM=" << (getPhysicalMemSz() + GB - 1) / GB << "GB " << dateAndTime << endl;
#ifdef COUNTER64
    cout << "COUNTER64";
#else
    cout << "COUNTER32";
#endif
#ifdef FALSESHARING
    cout << " FALSESHARING";
#endif
    cout << " NOPS=" << NOPS << " NSECONDS=" << NSECONDS << " TREETYP=" << TREESTR;
    cout << endl;
    cout << "Intel" << (cpu64bit() ? "64" : "32") << " family " << cpuFamily() << " model " << cpuModel() << " stepping " << cpuStepping() << " " << cpuBrandString() << endl;

    //
    // get cache info
    //
    lineSz = getCacheLineSz();
    //lineSz *= 2;

    if ((&cnt3 >= &cnt0) && (&cnt3 < (&cnt0 + lineSz/sizeof(UINT64))))
        cout << "Warning: cnt3 shares cache line used by cnt0" << endl;
    if ((&cnt3 >= &cnt1) && (&cnt3 < (&cnt1 + lineSz / sizeof(UINT64))))
        cout << "Warning: cnt3 shares cache line used by cnt1" << endl;
    if ((&cnt3 >= &cnt2) && (&cnt3 < (&cnt2 + lineSz / sizeof(UINT64))))
        cout << "Warning: cnt2 shares cache line used by cnt1" << endl;

#if TREETYP == 3

    //
    // check if RTM supported
    //
    if (!rtmSupported()) {
        cout << "RTM (restricted transactional memory) NOT supported by this CPU" << endl;
        quit();
        return 1;
    }

#endif

    cout << endl;

    //
    // allocate global variable
    //
    // NB: each element in g is stored in a different cache line to stop false sharing
    //
    threadH = (THREADH*) ALIGNED_MALLOC(maxThread*sizeof(THREADH), lineSz);             // thread handles
    ops = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);                   // for ops per thread

#if TREETYP == 3
    aborts = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);                // for counting aborts
#endif

#ifdef FALSESHARING
    g = (VINT*) ALIGNED_MALLOC((maxThread+1)*sizeof(VINT), lineSz);                     // local and shared global variables
#else
    g = (VINT*) ALIGNED_MALLOC((maxThread + 1)*lineSz, lineSz);                         // local and shared global variables
#endif

    r = (Result*) ALIGNED_MALLOC(5*maxThread*sizeof(Result), lineSz);                   // for results
    memset(r, 0, 5*maxThread*sizeof(Result));                                           // zero

    indx = 0;

    //
    // use thousands comma separator
    //
    setCommaLocale();

    //
    // header
	cout << "bound";
	cout << setw(9) << "nt";
	cout << setw(10) << "rt";
	cout << setw(14) << "ops";
	cout << setw(14) << "ops/sec";
	cout << setw(12) << "rel";
	cout << setw(14) << "treeSize";
	cout << setw(14) << "balanced";
	cout << endl;

	cout << "-----";              // current bound
	cout << setw(9) << "--";	  // num threads
	cout << setw(10) << "--";     // runtime  
	cout << setw(14) << "---";    // operations
	cout << setw(14) << "---";    // operations per sec
	cout << setw(12) << "---";    // relative proportion of operations compared to 1 thread
	cout << setw(14) << "------"; // size of the tree
	cout << setw(14) << "------"; // is tree balanced
	cout << endl;

    //
    // run tests
    //
    UINT64 ops1 = 1;
	current_bound = 0;

	for (current_bound = 0; current_bound < sizeof(key_ranges)/sizeof(int); current_bound++) {
		
		// Double the number of threads we are using, up to 2*ncpu
		for (int nt = 1; nt <= maxThread; nt*=2, indx++) {
			
			// Half-fill the binary tree to start with, within the range of values given.
			prefillBinaryTree(0, key_ranges[current_bound]);

            //
            //  zero shared memory
            //
            for (int thread = 0; thread < nt; thread++)
                *(GINDX(thread)) = 0;   // thread local
            *(GINDX(maxThread)) = 0;    // shared

            //
            // get start time
            //
            tstart = getWallClockMS();

            //
            // create worker threads
            //
            for (int thread = 0; thread < nt; thread++)
                createThread(&threadH[thread], worker, (void*)(size_t)thread);

            //
            // wait for ALL worker threads to finish
            //
            waitForThreadsToFinish(nt, threadH);
            UINT64 rt = getWallClockMS() - tstart;

            //
            // save results and output summary to console
            //
			for (int thread = 0; thread < nt; thread++)
			{
				r[indx].ops += ops[thread];
				r[indx].incs += *(GINDX(thread));
			}
			r[indx].incs += *(GINDX(maxThread));
			if ((sharing == 0) && (nt == 1))
				ops1 = r[indx].ops;
			r[indx].sharing = sharing;
			r[indx].nt = nt;
			r[indx].rt = rt;

			cout << setw(8) << key_ranges[current_bound];
			cout << setw(8) << nt;
			cout << setw(10) << fixed << setprecision(2) << (double)rt / 1000;
			cout << setw(14) << r[indx].ops;
			cout << setw(14) << r[indx].ops/r[indx].rt;
			cout << setw(12) << fixed << setprecision(2) << (double)r[indx].ops / ops1;
			cout << setw(14) << tree->sizeOfTree(tree->root);
			bool balanced = tree->checkTreeBalanced();
			if (balanced) cout << setw(14) << "Yes";
			else cout << setw(14) << "No";


#if TREETYP == 3
            cout << setw(7) << fixed << setprecision(0) << 100.0 * (r[indx].ops - r[indx].aborts) / r[indx].ops << "%";
#endif
            cout << endl;

            //
            // delete thread handles
            //
            for (int thread = 0; thread < nt; thread++)
                closeThread(threadH[thread]);

        }

    }

    cout << endl;

    quit();

    return 0;

}