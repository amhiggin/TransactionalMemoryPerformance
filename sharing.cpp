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

using namespace std;                            // cout

#define K           1024                        //
#define GB          (K*K*K)                     //
#define NOPS        10000                       //
#define NSECONDS    2                           // run each test for NSECONDS

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
BST *tree;


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


#define TREETYP	0							//set up tree type

#if TREETYPE == 0
#define TREESTR "BST_LOCK"
#define ADDNODE(n)			addNodeTATASLock(n);
#define REMOVENODE(key)		removeNodeTATASLock(key);

#elif TREETYP == 1
#define TREESTR "BST__HLE"
#define ADDNODE(n)			addNodeHLE(n);
#define REMOVENODE(key)		removeNodeHLE(key);

#elif TREETYP == 2
#define TREESTR "BST_RTM"

#endif

void addNodeTATASLock(Node* n) {
	tree->acquireLock();
	tree->insertNode(n);
	tree->releaseLock();
}

void addNodeHLE(Node* n) {
	tree->acquireHLE();
	tree->insertNode(n);
	tree->releaseHLE();
}

Node* removeNodeTATASLock(int key) {
	tree->acquireLock();
	Node* removed = tree->removeNode(key);
	tree->releaseLock();
	printf("Removed node %I64d\n", removed->key);
	return removed;
}

Node* removeNodeHLE(int key) {
	tree->acquireHLE();
	Node* removed = tree->removeNode(key);
	tree->releaseHLE();
	return removed;
}

// Generate a pseudo-random integer.
int generateRandomKey() {
	UINT *random_number = new UINT;
	*random_number = rand(*random_number);
	int random_key = *random_number % key_ranges[current_bound];
	printf("Random key is: %d\n", random_key);
	return random_key;
}

// This method should pre-fill the binary tree so that it is half full starting off.
void prefillBinaryTree(int min_value, int max_value) {
	tree = new BST();

	int num_nodes = (max_value - min_value) / 2;

	for (int i = 0; i < num_nodes; i++) {
		int key = generateRandomKey();
		Node* n = new Node(key);
		tree->insertNode(n);
	}
	printf("Prefilled binary tree to half-full.\n");
}

// ReuseQ is used to prevent unnecessary inhibition of parallelism
queue<Node*> make_empty_reuseq() {
	queue<Node*> ReuseQueue;
	for (int i = 0; i < 1000; i++) {
		ReuseQueue.push(new Node(0));
	}
	printf("Initialised empty reuseQ successfully.\n");
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
			// check last bit of random num
			int update = random_key & 1;

			if (update == 1) {
				Node *n_add;

				if (!ReuseQueue.empty()) {
					// Take a node off the reuseQ
					n_add = ReuseQueue.front();
					ReuseQueue.pop();
					n_add->key = random_key;
					printf("Used node off the ReuseQ\n");
				} else {
					n_add = new Node(random_key);
				}
				ADDNODE(n_add);

			} else {
				// We are removing this node if it exists
				Node* n_remove = REMOVENODE(random_key);
				
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
	// TODO deallocate all of the memory given to the  tree nodes before exit
	tree->deleteTree(tree->root);
	tree->root = NULL;
	return 0;

}

//
// main
//
int main()
{
    ncpu = getNumberOfCPUs();   // number of logical CPUs
    maxThread = 2 * ncpu;       // max number of threads

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
    //
    cout << "sharing";
    cout << setw(4) << "nt";
    cout << setw(6) << "rt";
    cout << setw(16) << "ops";
    cout << setw(6) << "rel";
#if TREETYP == 3
    cout << setw(8) << "commit";
#endif
    cout << endl;

    cout << "-------";              // sharing
    cout << setw(4) << "--";        // nt
    cout << setw(6) << "--";        // rt
    cout << setw(16) << "---";      // ops
    cout << setw(6) << "---";       // rel
#if TREETYP == 3
    cout << setw(8) << "------";
#endif
    cout << endl;

    //
    // boost process priority
    // boost current thread priority to make sure all threads created before they start to run
    //
#ifdef WIN32
//  SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
//  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    //
    // run tests
    //
    UINT64 ops1 = 1;
	current_bound = 0;

	for (current_bound = 0; current_bound < sizeof(key_ranges)/sizeof(int); current_bound++) {
		// Half-fill the binary tree to start with, within the range of values given.
		prefillBinaryTree(0, key_ranges[current_bound]);
		
		for (int nt = 1; nt <= maxThread; nt++, indx++) {

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
            for (int thread = 0; thread < nt; thread++) {
                r[indx].ops += ops[thread];
                r[indx].incs += *(GINDX(thread));
#if TREETYP == 3
                r[indx].aborts += aborts[thread];
#endif
            }
            r[indx].incs += *(GINDX(maxThread));
            if ((sharing == 0) && (nt == 1))
                ops1 = r[indx].ops;
            r[indx].sharing = sharing;
            r[indx].nt = nt;
            r[indx].rt = rt;

            //cout << setw(6) << sharing << "%";
            cout << setw(4) << nt;
            cout << setw(6) << fixed << setprecision(2) << (double) rt / 1000;
            cout << setw(16) << r[indx].ops;
            cout << setw(6) << fixed << setprecision(2) << (double) r[indx].ops / ops1;

#if TREETYP == 3

            cout << setw(7) << fixed << setprecision(0) << 100.0 * (r[indx].ops - r[indx].aborts) / r[indx].ops << "%";

#endif

            if (r[indx].ops != r[indx].incs)
                cout << " ERROR incs " << setw(3) << fixed << setprecision(0) << 100.0 * r[indx].incs / r[indx].ops << "% effective";

            cout << endl;

            //
            // delete thread handles
            //
            for (int thread = 0; thread < nt; thread++)
                closeThread(threadH[thread]);

        }

    }

    cout << endl;

    //
    // output results so they can easily be pasted into a spread sheet from console window
    //
    setLocale();
    cout << "sharing/nt/rt/ops/incs";
#if TREETYP == 3
    cout << "/aborts";
#endif
    cout << endl;
    for (UINT i = 0; i < indx; i++) {
        cout << r[i].sharing << "/"  << r[i].nt << "/" << r[i].rt << "/"  << r[i].ops << "/" << r[i].incs;
#if TREETYP == 3
        cout << "/" << r[i].aborts;
#endif
        cout << endl;
    }
    cout << endl;

    quit();

    return 0;

}

// eof