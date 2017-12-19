#include <iostream>
#include <string.h>
#include "BST.h"
#include <cmath>
#ifdef WIN32
#include <intrin.h>
#endif
#include "helper.h"

using namespace std;

BST::BST() {
	root = NULL;
	lock = 0;
	nabort = 0;
	return;
}

int BST::contains(INT64 key) {
	bool found = false;

	if (root == NULL) {
		return -1;
	}
	else {
		Node volatile *curr = root;

		while (curr != NULL && found == false) {
			int result = key - curr->key;
			if (result < 0) {
				// go left
				curr = curr->left;
			}
			else if (result > 0) {
				// go right
				curr = curr->right;
			}
			else {
				// Found the key
				return 1;
			}
		}
	}
	// didn't find the key
	return 0;
}

// NOTE: allocate Node* n outside of the method, to minimise the size of the transaction inside
INT64 BST::insertNode(Node *n) {
	Node* volatile* volatile  pp = &root;
	Node* volatile p = root;
	while (p) { // find position in tree
		if (n->key < p->key) {
			pp = &p->left; // go left
		}
		else if (n->key > p->key) {
			pp = &p->right; // go right
		}
		else {
			return 0; // return 0 if key already in tree
		}
		p = *pp; // next Node
	}
	*pp = n; // add Node n

	return 1;
}


Node* volatile BST::removeNode(INT64 key) {
	Node* volatile* volatile pp = &root;
	Node* volatile p = root;

	while (p) { // find key
		if (key < p->key) {
			pp = &p->left; // go left
		}
		else if (key > p->key) {
			pp = &p->right; // go right
		}
		else {
			break;
		}
		p = *pp; // next Node
	}
	if (p == NULL) {// NOT found
		return NULL;
	}

	if (p->left == NULL && p->right == NULL) {
		*pp = NULL; // NO children
	}
	else if (p->left == NULL) {
		*pp = p->right; // ONE child
	}
	else if (p->right == NULL) {
		*pp = p->left; // ONE child
	}
	else {
		Node* volatile r = p->right; // TWO children
		Node* volatile* volatile ppr = &p->right; // find min key in right sub tree
		while (r->left) {
			ppr = &r->left;
			r = r->left;
		}
		p->key = r->key; // could move...
		p = r; // node instead
		*ppr = r->right;
	}
	// Return removed node so that we can put it on ReuseQ
	return p;
}


// Recursive function to find the size of the tree.
int BST::sizeOfTree(Node *node) {
	if (node == NULL) {
		return 0;
	} 
	return (sizeOfTree(node->left) + 1 + sizeOfTree(node->right));
}

void BST::deleteTree(Node volatile *next) {
	if (next != NULL) {
		deleteTree(next->left);
		deleteTree(next->right);
	}
}


int BST::computeHeight(Node *n) {
	if (n == NULL) {
		return 0;
	}
	// Recurse down left and right subtrees
	int leftChildHeight = computeHeight(n->left);
	if (leftChildHeight == -1) {
		return -1;
	}
	
	int rightChildHeight = computeHeight(n->right);
	if (rightChildHeight == -1) {
		return -1;
	}
	
	int difference = leftChildHeight - rightChildHeight;

	if (abs(difference) > 1) {
		return -1;
	}
	else {
		return max(leftChildHeight, rightChildHeight) + 1;
	}
}

bool BST::checkTreeBalanced() {
	if (computeHeight(root) == -1){
		return false;
	}
	return true;
}


/*
	TATAS Lock methods.
*/
void BST::acquireLock() {
#ifdef WIN32
while (InterlockedExchange(&lock, 1)) // try for lock
		while (lock == 1) // wait until lock free
			_mm_pause(); // instrinsic see next slide
#elif __linux__
	while (__sync_lock_test_and_set(&lock, 1))
		while (lock == 1)
			_mm_pause();
#endif
}

void BST::releaseLock() {
	lock = 0;
}


/*
	HLE Methods.
*/

void BST::acquireHLE() {
#ifdef WIN32
	while (_InterlockedExchange_HLEAcquire(&lock, 1) == 1) {
		nabort++;
		do {
			_mm_pause();
		} while (lock == 1);
	}
#elif __linux
	while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE | __ATOMIC_HLE_ACQUIRE)){																			\
		nabort++;																
		do {																		
			_mm_pause();														
		} while (lock == 1);													
	}
#endif
}

void BST::releaseHLE() {
#ifdef WIN32
	_Store_HLERelease(&lock, 0);
#elif __linux__
	__atomic_store_n(&lock, 0, __ATOMIC_RELEASE | __ATOMIC_HLE_RELEASE);
#endif
}



