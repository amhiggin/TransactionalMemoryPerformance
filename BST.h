/*
	BST.h, header for Binary Search Tree class.
*/
#ifndef BST_H
#define BST_H

#include <iostream>
#include "TestAndTestAndSetLock.h"
using namespace std;

struct Node {
	INT64 key;
	Node *left;
	Node *right;
};

class BST {

public:
	Node *root;
	BST();

	Node *findNode(INT64 key);
	INT64 insertNode(Node *n);
	Node *removeNode(INT64 key);

#ifndef BST_LOCK
	TestAndTestAndSetLock lock;
	void acquireLock();
	void releaseLock();
#endif
#ifndef BST_HLE
	void acquireHLE();
	void releaseHLE();
#endif

};

#endif