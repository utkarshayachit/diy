#ifndef DIY_ALGORITHMS_HPP
#define DIY_ALGORITHMS_HPP

#include <vector>

#include "master.hpp"
#include "assigner.hpp"
#include "reduce.hpp"
#include "reduce-operations.hpp"
#include "partners/swap.hpp"

#include "detail/algorithms/sort.hpp"
#include "detail/algorithms/kdtree.hpp"

namespace diy
{

//! sample sort `values` of each block, store the boundaries between blocks in `samples`
template<class Block, class T, class Cmp>
void sort(Master&                   master,
          const Assigner&           assigner,
          std::vector<T> Block::*   values,
          std::vector<T> Block::*   samples,
          size_t                    num_samples,
          const Cmp&                cmp,
          int                       k   = 2,
          bool                      samples_only = false)
{
  bool immediate = master.immediate();
  master.set_immediate(false);

  // NB: although sorter will go out of scope, its member functions sample()
  //     and exchange() will return functors whose copies get saved inside reduce
  detail::SampleSort<Block,T,Cmp> sorter(values, samples, cmp, num_samples);

  // swap-reduce to all-gather samples
  RegularSwapPartners   partners(1, assigner.nblocks(), k);
  reduce(master, assigner, partners, sorter.sample(), detail::SkipIntermediate(partners.rounds()));

  // all_to_all to exchange the values
  if (!samples_only)
      all_to_all(master, assigner, sorter.exchange(), k);

  master.set_immediate(immediate);
}

template<class Block, class T>
void sort(Master&                   master,
          const Assigner&           assigner,
          std::vector<T> Block::*   values,
          std::vector<T> Block::*   samples,
          size_t                    num_samples,
          int                       k   = 2)
{
    sort(master, assigner, values, samples, num_samples, std::less<T>(), k);
}

template<class Block, class Point>
void kdtree(Master&                         master,
            const Assigner&                 assigner,
            int                             dim,
            const ContinuousBounds&         domain,
            std::vector<Point>  Block::*    points,
            size_t                          bins,
            bool                            wrap = false)
{
    if (assigner.nblocks() & (assigner.nblocks() - 1))
    {
        fprintf(stderr, "KD-tree requires a number of blocks that's a power of 2, got %d\n", assigner.nblocks());
        std::abort();
    }

    typedef     diy::RegularContinuousLink      RCLink;

    for (int i = 0; i < master.size(); ++i)
    {
        RCLink* link   = static_cast<RCLink*>(master.link(i));
        link->core()   = domain;
        link->bounds() = domain;
    }

    detail::KDTreePartition<Block,Point>    kdtree_partition(dim, points, bins);

    detail::KDTreePartners                  partners(dim, assigner.nblocks(), wrap, domain);
    reduce(master, assigner, partners, kdtree_partition);

    // update master.expected to match the links
    int expected = 0;
    for (int i = 0; i < master.size(); ++i)
      expected += master.link(i)->size_unique();
    master.set_expected(expected);
}

}

#endif
