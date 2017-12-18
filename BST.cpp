#include <iostream>
#include <string.h>
#include "BST.h"

using namespace std;

BST::BST() {
	root = NULL;
	return;
}


INT64 BST::insertNode(Node *n) {

	Node **pp = &root;
	Node *p = root;
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



Node* BST::removeNode(INT64 key) {
	Node **pp = &root;
	Node *p = root;

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
	if (p == NULL) // NOT found
		return NULL;

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
		Node *r = p->right; // TWO children
		Node **ppr = &p->right; // find min key in right sub tree
		while (r->left) {
			ppr = &r->left;
			r = r->left;
		}
		p->key = r->key; // could move...
		p = r; // node instead
		*ppr = r->right;
	}
	return p; // return removed node}

#ifndef BST_LOCK
void BST::acquireLock() {
	lock.acquire();
}
void BST::releaseLock() {
	lock.release();
}
#endif


#ifndef BST_HLE
	void BST::acquireHLE() {
		// TODO
	}

	void BST::releaseHLE() {
		// TODO
	}
#endif

