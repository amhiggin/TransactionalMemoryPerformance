/*
	BST.h, header for Binary Search Tree class.
*/

#include <iostream>
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
	// Store lock in own cache line for performance
	ALIGN(64) volatile long lock;
	int nabort; // HLE/RTM
	
	BST();

	int contains(INT64 key);
	int sizeOfTree(Node *node);
	void deleteTree(Node volatile *next);
	int computeHeight(Node *n);
	bool checkTreeBalanced();

	INT64 insertNode(Node *n);
	Node* volatile removeNode(INT64 key);
	
	void acquireLock();
	void releaseLock();
	void acquireHLE();
	void releaseHLE();

};