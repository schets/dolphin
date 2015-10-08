// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

// a simple lockless thread-safe,
// single reader, single writer queue

#include <algorithm>
#include <utility>
#include <atomic>
#include <new>

#include <cstddef>

#include "Common/CommonTypes.h"

namespace Common
{

template <typename T, bool NeedSize = true>
class FifoQueue
{
public:
	FifoQueue()
	{
		init();
	}

	~FifoQueue()
	{
		destroy();
	}

	u32 Size() const
	{
		return tail.load(std::memory_order_relaxed) - head;
	}

	bool Empty() const
	{
		return Size() == 0;
	}

	//this is quite invalid on an empty queue...
	T& Front() const
	{
		return head_block[head];
	}

	template <typename ...Arg>
	void Push(Arg&&... t)
	{
		auto ctail = tail.load(std::memory_order_relaxed);
		auto cind = ctail & nmod;
		if (cind == 0) {
			auto nb = get_block();

			//this can be relaxed, since the release store on tail
			//acts as a synchronizer - head can not read this
			//until it reads the increased tail, at which point this
			//store is good to go!
			tail_block->next.store(nb, std::memory_order_relaxed);
			tail_block = nb;

		}

		new (&tail_block->elems[cind]) T(std::forward<Arg>(t)...);
		tail.store(ctail + 1, std::memory_order_release);
	}

	void Pop()
	{
		dopop<false>(nullptr);
	}

	bool Pop(T& t)
	{
		return dopop<true>(&t);
	}

	// not thread-safe
	void Clear()
	{
		//extra alloc, but way simpler...
		destroy();
		init();
	}

private:

	void init(){
		head_block = get_block();
		tail_block = head_block;
		head = 1;
		tail_cache = 1;
		tail.store(1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
	}

	void destroy() {
		std::atomic_thread_fence(std::memory_order_acquire);
		while (Pop());
		while (head) {
			dptr = head_block;
			head = head_block->next.load(std::memory_order_relaxed);
			return_block(dptr);
		}
	}

	template<bool docreate>
	bool dopop(T *dop)
	{
		auto chead = head;
		if (chead == tail_cache) {
			tail_cache = tail.load(std::memory_order_acquire);
			if (chead == tail_cache) {
				return false;
			}
		}

		head++;
		auto hind = chead & nmod;
		if (hind == 0) {
			auto ohead = head_block;
			//this can be relaxed, since the acquire on tail ensures that
			//this won't be reordered around a store to next on this
			head_block = head_block->next.load(std::memory_order_relaxed);
			return_block(ohead);
		}

		T &curv = head_block->elems[hind];

		if (docreate) {
			*dop = std::move(curv);
		}
		else {
			curv.~T();
		}
		return true;
	}

	struct queue_block;

	queue_block *get_block() {
		return malloc(sizeof(queue_block));
	}

	void return_block(queue_block *qb) {
		free(qb);
	}
	//kinda of a little namespace!
	struct get_size {

		constexpr static size_t get_big() {
			return sizeof(T) < 1024 ? 8 : 4;
		}

		constexpr static size_t get_med() {
			return sizeof(T) <= 128 ? 32 : get_big();
		}

		constexpr static size_t size() {
			return sizeof(T) <= 32 ? 128 : get_med();
		}

	};

	constexpr static size_t nelems = get_size::size();
	constexpr static size_t nmod = nelems - 1;

	struct queue_block {
		T elems[nelems];
		std::atomic<queue_block *> next;
	};

	std::atomic<size_t> tail;
	queue_block *tail_block;

	size_t head;
	size_t tail_cache;
	queue_block *head_block;
};
