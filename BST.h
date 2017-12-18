/*
	BST.h, header for Binary Search Tree class.
*/

#include <iostream>
#include "TestAndTestAndSetLock.h"
#include "HLE.h"
#include "helper.h"
using namespace std;

class Node {

public:
	// Need to declare these as volatile because our program is multi-threaded.
	INT64 volatile key;
	Node* volatile left;
	Node* volatile right;

	Node(int _key) {
		key = _key;
		left = right = NULL;
	}
};

class BST {

public:
	Node* volatile root;
	ALIGN(64) volatile long lock;
	
	BST();

	int contains(INT64 key);
	int sizeOfTree(Node volatile *node);
	void deleteTree(volatile Node* next);

	INT64 insertNode(Node *n);
	Node* volatile removeNode(INT64 key);
	
#ifndef BST_LOCK
	void acquireLock();
	void releaseLock();
#endif 
#ifndef BST_HLE
	void acquireHLE();
	void releaseHLE();
#endif
	// TODO implement methods for RTM

};