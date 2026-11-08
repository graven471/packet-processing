#include "dpdk.h"
#include <rte_common.h>
#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_lcore_var.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_per_lcore.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * core components of DPDK
 * 1. rte_timer : Timer facilities. Based on HPET interface that is provided by
 * EAL
 * 2. rte_malloc : Allocation of named memory zones using libc's malloc
 * 3. rte_mempool : Handle a pool of objects using ring to store them. Allow
 * bulk enqueue/dequeue and per-CPU cache.
 * 4. rte_eal + libc : Environment abstraction layer: RTE loading, memory
 * allocation, PCI access, logging etc
 * 5. rte_mbuf : Manipulation of packet buffers carrying network data
 * 6. rte_ring : Fixed-size lockless FIFO for storing objects in table
 * 7. rte_debug : Provides debug helpers
 */

static inline void init_eal() {
  char *argv[] = {
      "packet-processor",
      "-l",
      "0-10", // lcores 0 to 10
      "-n",
      "4", // memory channels
           // "--no-huge", // for dev/testing without hugepages
  };
  int argc = sizeof(argv) / sizeof(argv[0]);

  int ret;
  ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_panic("Cannot init EAL\n");

  RTE_LOG(INFO, USER1, "EAL initialization success\n");
}

struct lcore_state {
  int a;
  int b;
};

// this lcore_states points to the starting mem_address where struct lcore_state
// is allocated each CPU have its own data no sharing
static RTE_LCORE_VAR_HANDLE(struct lcore_state, lcore_states);
RTE_LCORE_VAR_INIT(lcore_states);

RTE_INIT(rte_state_init) {
  RTE_LCORE_VAR_ALLOC(lcore_states);

  unsigned int lcore_id;
  struct lcore_state *state;
  RTE_LCORE_VAR_FOREACH(lcore_id, state, lcore_states) {
    state->a = 20;
    state->b = 30;
  }
}

struct foo {
  const char *name;
  uint32_t id;
};

// =============== chapter 2 Memory pool libraries ============
static inline void mem_pool_lib() {
  // a memory pool is an allocator of a fixed-size object (conceptually same as
  // slab) In the DPDK, it is identified by name and uses a mempool handler to
  // store free objects. default mempool handler is a ring based.

  // memory pool’s ring of free buffers is MPMC it requires CAS operation to
  // access it DPDK docs says: To avoid having too many access requests to the
  // memory pool’s ring, the memory pool allocator can maintain a per-core cache
  // and do bulk requests to the memory pool’s ring, via the cache with many
  // fewer locks on the actual memory pool structure. In this way, each core has
  // full access to its own cache (without locks) of free objects and only when
  // the cache fills does the core need to shuffle some of the free objects back
  // to the pools ring or obtain more objects when the cache is empty
  //
  // here is how i think it works:
  // consider ring of free buffers is a power of two ring no wrapping
  // initially we got a page from mmap say base = 0x1000 and size = 4096
  // our object_size is 32 so the formula is this:
  // B(i) = base + i * object_size, where i is the slot index
  // so we did this and fill up our ring buffer concepually it looks something
  // like this: Object 0  -> 0x1000 Object 1  -> 0x1020 Object 2  -> 0x1040
  // Object 3  -> 0x1060
  // ...
  // Object 127 -> 0x1FE0
  // now instead of fetching a free block from this ring everything during init
  // we can store say 20 object ptr(s) in each CPU cache so, CPU0 stores
  // object0..19, CPU1 stores object 20..39 etc and during allocation we check
  // if cache is not empty then just return from cache no lock no CAS nothing
  // otherwise do CAS and bulk dequeue from shared ring into target cache.
  // that's the idea if i get it correctly

  struct rte_mempool *first_pool = rte_mempool_create(
      "test",                   /* name of the pool */
      sizeof(struct foo) * 256, /* number of elements in the mempool */
      sizeof(struct foo),       /* The size of each element.  */
      256, /* cache_size thing i described above each CPU will have 256 blocks
              in there private cache this avoids CAS locks and accessing shared
              ring for each allocation */
      0,   /* size of the private data (todo): learn about this */
      nullptr, /* A function pointer that is called for initialization of the
                  pool, before object initialization. */
      nullptr, /* An opaque pointer to data that can be used in the mempool
                  constructor function */
      nullptr, /* A function pointer that is called for each object at
                  initialization of the pool. The user can set some meta data in
                  objects if needed */
      nullptr, /* An opaque pointer to data that can be used as an argument for
                  each call to the object constructor function.  */
      SOCKET_ID_ANY, /* The socket_id argument is the socket identifier in the
                        case of NUMA */
      RTE_MEMPOOL_F_NO_SPREAD /* The flags arguments is an OR of following
                                 flags: RTE_MEMPOOL_F_NO_SPREAD,
                                 RTE_MEMPOOL_F_NO_CACHE_ALIGN,
                                 RTE_MEMPOOL_F_SP_PUT, RTE_MEMPOOL_F_SC_GET,
                                 RTE_MEMPOOL_F_NO_IOVA_CONTIG */
  );

  if (first_pool == nullptr) {
    rte_exit(EXIT_FAILURE, "mempool create failed: %s\n",
             rte_strerror(rte_errno));
  }

  RTE_LOG(INFO, USER1, "Mempool created successfully %p\n", first_pool);

  // now we can access individual objects
  struct foo *obj;

  if (rte_mempool_get(first_pool, (void **)&obj) == 0) {
    obj->name = "hey";
    obj->id = 20;

    // return the object to pool
    rte_mempool_put(first_pool, obj);
  }

  // we can get bulk objects like this
  constexpr unsigned int max_object_count = 20;

  struct foo *objects[max_object_count];

  if (rte_mempool_get_bulk(first_pool, (void **)objects, max_object_count) ==
      0) {
    for (unsigned i = 0; i < max_object_count; i++) {
      objects[i]->name = "foo";
      objects[i]->id = 40 * i;

      rte_mempool_put_bulk(first_pool, (void **)objects, max_object_count);
    }
  }

  // free the pool
  rte_mempool_free(first_pool);
  first_pool = nullptr;
}

// roadmap = rte_mempool → rte_mbuf → rte_ethdev (rx)
void learn() {
  init_eal();

  rte_state_init();
  const struct lcore_state *state = RTE_LCORE_VAR_LCORE(0, lcore_states);
  printf("a = %d, b = %d\n", state->a, state->b);

  mem_pool_lib();
}
