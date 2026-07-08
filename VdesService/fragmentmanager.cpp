// fragmentmanager.cpp
#include "fragmentmanager.h"
#include <iostream>
using namespace std;

FragmentManager::FragmentManager() {}

void FragmentManager::setResourceAllocationFragment(const ResourceAllocationFragment& fragment) {
    m_resourceFragment = fragment;
    cout << "[FragmentManager] setResourceAllocationFragment, onReady will be called" << endl;   // 新增
    if (onReady) onReady();
}

void FragmentManager::setStartFragment(const BaseFragment& fragment) {
    m_startFragment = fragment;
    cout << "[FragmentManager] setStartFragment, fragmentNum=" << fragment.fragmentNum << endl;   // 新增
    if (onStart) onStart();
}

void FragmentManager::addContinueFragment(const BaseFragment& fragment) {
    m_continueFragments.push_back(fragment);
    cout << "[FragmentManager] addContinueFragment, total now=" << m_continueFragments.size() << endl;   // 新增
    if (onContinue) onContinue();
}

void FragmentManager::setEndFragment(const BaseFragment& fragment) {
    m_endFragment = fragment;
    cout << "[FragmentManager] setEndFragment, fragmentNum=" << fragment.fragmentNum << endl;   // 新增
    if (onEnd) onEnd();
}

void FragmentManager::clearContinueFragments() {
    m_continueFragments.clear();
}