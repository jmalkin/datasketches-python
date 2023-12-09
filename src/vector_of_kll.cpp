/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string.h>

#include "kll_sketch.hpp"

namespace nb = nanobind;

namespace datasketches {

namespace vector_of_kll_constants {
  static const uint32_t DEFAULT_K = kll_constants::DEFAULT_K;
  static const uint32_t DEFAULT_D = 1;
}

// Wrapper class for Numpy compatibility
template <typename T, typename C = std::less<T>>
class vector_of_kll_sketches {
  public:
    explicit vector_of_kll_sketches(uint32_t k = vector_of_kll_constants::DEFAULT_K, uint32_t d = vector_of_kll_constants::DEFAULT_D);
    vector_of_kll_sketches(const vector_of_kll_sketches& other);
    vector_of_kll_sketches(vector_of_kll_sketches&& other) noexcept;
    vector_of_kll_sketches<T, C>& operator=(const vector_of_kll_sketches& other);
    vector_of_kll_sketches<T, C>& operator=(vector_of_kll_sketches&& other);

    // container parameters
    inline uint32_t get_k() const;
    inline uint32_t get_d() const;

    template<typename V>
    using array1d = nb::ndarray<V, nb::numpy, nb::ndim<1>>;

    template<typename V>
    using array2d = nb::ndarray<V, nb::numpy, nb::ndim<2>>;

    // sketch updates/merges
    void update(const nb::ndarray<T>& items);
    void merge(const vector_of_kll_sketches<T>& other);

    // returns a single sketch combining all data in the array
    kll_sketch<T, C> collapse(nb::ndarray<int>& isk) const;

    // sketch queries returning an array of results
    array1d<bool> is_empty() const;
    array1d<uint64_t> get_n() const;
    array1d<bool> is_estimation_mode() const;
    array1d<T> get_min_values() const;
    array1d<T> get_max_values() const;
    array1d<uint32_t> get_num_retained() const;
    array2d<T> get_quantiles(nb::ndarray<double>& ranks, nb::ndarray<int>& isk) const;
    array2d<double> get_ranks(nb::ndarray<T>& values, nb::ndarray<int>& isk) const;
    array2d<double> get_pmf(nb::ndarray<T>& split_points, nb::ndarray<int>& isk) const;
    array2d<double> get_cdf(nb::ndarray<T>& split_points, nb::ndarray<int>& isk) const;

    // human-readable output
    std::string to_string(bool print_levels = false, bool print_items = false) const;

    // binary output/input
    nb::list serialize(nb::ndarray<int>& isk);
    // note: deserialize() replaces the sketch at the specified
    //       index. Not a static method.
    void deserialize(const nb::bytes& sk_bytes, uint32_t idx);

  private:
    std::vector<uint32_t> get_indices(array1d<int>& isk) const;
    
    char check_order(nb::ndarray<T, nb::f_contig>) { return 'F'; }
    char check_order(nb::ndarray<T, nb::c_contig>) { return 'C'; }
    char check_order(nb::ndarray<T>) { return '?'; }

    const uint32_t k_; // kll sketch k parameter
    const uint32_t d_; // number of dimensions (here: sketches) to hold
    std::vector<kll_sketch<T, C>> sketches_;
};

template<typename T, typename C>
vector_of_kll_sketches<T, C>::vector_of_kll_sketches(uint32_t k, uint32_t d):
k_(k), 
d_(d)
{
  // check d is valid (k is checked by kll_sketch)
  if (d < 1) {
    throw std::invalid_argument("D must be >= 1: " + std::to_string(d));
  }

  sketches_.reserve(d);
  // spawn the sketches
  for (uint32_t i = 0; i < d; i++) {
    sketches_.emplace_back(k);
  }
}

template<typename T, typename C>
vector_of_kll_sketches<T, C>::vector_of_kll_sketches(const vector_of_kll_sketches& other) :
  k_(other.k_),
  d_(other.d_),
  sketches_(other.sketches_)
{}

template<typename T, typename C>
vector_of_kll_sketches<T, C>::vector_of_kll_sketches(vector_of_kll_sketches&& other) noexcept :
  k_(other.k_),
  d_(other.d_),
  sketches_(std::move(other.sketches_))
{}

template<typename T, typename C>
vector_of_kll_sketches<T, C>& vector_of_kll_sketches<T, C>::operator=(const vector_of_kll_sketches& other) {
  vector_of_kll_sketches<T, C> copy(other);
  k_ = copy.k_;
  d_ = copy.d_;
  std::swap(sketches_, copy.sketches_);
  return *this;
}

template<typename T, typename C>
vector_of_kll_sketches<T, C>& vector_of_kll_sketches<T, C>::operator=(vector_of_kll_sketches&& other) {
  k_ = other.k_;
  d_ = other.d_;
  std::swap(sketches_, other.sketches_);
  return *this;
}

template<typename T, typename C>
uint32_t vector_of_kll_sketches<T, C>::get_k() const {
  return k_;
}

template<typename T, typename C>
uint32_t vector_of_kll_sketches<T, C>::get_d() const {
  return d_;
}

template<typename T, typename C>
std::vector<uint32_t> vector_of_kll_sketches<T, C>::get_indices(array1d<int>& isk) const {
  std::vector<uint32_t> indices;
  if (isk.size() == 1) {
    if (isk(0) == -1) {
      indices.reserve(d_);
      for (uint32_t i = 0; i < d_; ++i) {
        indices.push_back(i);
      }
    } else {
      indices.push_back(static_cast<uint32_t>(isk(0)));
    }
  } else {
    indices.reserve(isk.size());
    for (uint32_t i = 0; i < isk.size(); ++i) {
      const uint32_t idx = static_cast<uint32_t>(isk(i));
      if (idx < d_) {
        indices.push_back(idx);
      } else {
        throw std::invalid_argument("request for invalid dimensions >= d ("
                 + std::to_string(d_) +"): "+ std::to_string(idx));
      }
    }
  }
  return indices;
}

// Checks if each sketch is empty or not
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::is_empty() const -> array1d<bool> {
  std::vector<char> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].is_empty();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<bool>(vals.data(), 1, shape);
}

// Updates each sketch with values -- 1-d input
// Currently: all values must be present
// TODO: allow subsets of sketches to be updated
/*
template<typename T, typename C>
void vector_of_kll_sketches<T, C>::update(const nb::ndarray<T, nb::ndim<1>>& items) {
  if (items.size() != d_) {
    throw std::invalid_argument("input data must have rows with  " + std::to_string(d_)
          + " elements. Found: " + std::to_string(items.size()));   
  }

  // 1D case: single value to update per sketch
  //auto data = items.template unchecked<1>();
  for (uint32_t i = 0; i < d_; ++i) {
    sketches_[i].update(items(i));
  }
}

// Updates each sketch with values -- 2-d input
// Currently: all values must be present
// TODO: allow subsets of sketches to be updated
template<typename T, typename C>
void vector_of_kll_sketches<T, C>::update(const nb::ndarray<T, nb::ndim<2>>& items) {
  if (items.shape(1) != d_) {
    throw std::invalid_argument("input data must have rows with  " + std::to_string(d_)
          + " elements. Found: " + std::to_string(items.shape(1)));   
  }

  // 2D case: multiple values to update per sketch
  //auto data = items.template unchecked<2>();
  //if (items.flags() & nb::ndarray::f_style) {
  if constexpr (check_order(items) == 'F') {
    for (uint32_t j = 0; j < d_; ++j) {
      for (uint32_t i = 0; i < items.shape(0); ++i) { 
        sketches_[j].update(items(i,j));
      }
    }
  } else { // nb::c_contig or nb::any_contig
    for (uint32_t i = 0; i < items.shape(0); ++i) { 
      for (uint32_t j = 0; j < d_; ++j) {
        sketches_[j].update(items(i,j));
      }
    }
  }
}
*/

// Updates each sketch with values
// Currently: all values must be present
// TODO: allow subsets of sketches to be updated
template<typename T, typename C>
void vector_of_kll_sketches<T, C>::update(const nb::ndarray<T>& items) {
 
  size_t ndim = items.ndim();

  if (items.shape(ndim-1) != d_) {
    throw std::invalid_argument("input data must have rows with  " + std::to_string(d_)
          + " elements. Found: " + std::to_string(items.shape(ndim-1)));
  }
  
  if (ndim == 1) {
    // 1D case: single value to update per sketch
    //auto data = items.template unchecked<1>();
    for (uint32_t i = 0; i < d_; ++i) {
      sketches_[i].update(items(i));
    }
  }
  else if (ndim == 2) {
    // 2D case: multiple values to update per sketch
    //auto data = items.template unchecked<2>();
    if (check_order(items) == 'F') {
      for (uint32_t j = 0; j < d_; ++j) {
        for (uint32_t i = 0; i < items.shape(0); ++i) { 
          sketches_[j].update(items(i,j));
        }
      }
    } else { // nb::c_contig or nb::any_contig
      for (uint32_t i = 0; i < items.shape(0); ++i) { 
        for (uint32_t j = 0; j < d_; ++j) {
          sketches_[j].update(items(i,j));
        }
      }
    }
  }
  else {
    throw std::invalid_argument("Update input must be 2 or fewer dimensions : " + std::to_string(ndim));
  }
}


// Merges two arrays of sketches
// Currently: all values must be present
template<typename T, typename C>
void vector_of_kll_sketches<T, C>::merge(const vector_of_kll_sketches<T>& other) {
  if (d_ != other.get_d()) {
    throw std::invalid_argument("Must have same number of dimensions to merge: " + std::to_string(d_)
                                + " vs " + std::to_string(other.d_));
  } else {
    for (uint32_t i = 0; i < d_; ++i) {
      sketches_[i].merge(other.sketches_[i]);
    }
  }
}

template<typename T, typename C>
kll_sketch<T, C> vector_of_kll_sketches<T, C>::collapse(nb::ndarray<int>& isk) const {
  auto inds = isk.view<int, nb::ndim<1>>();
  
  kll_sketch<T, C> result(k_);
  for (int idx = 0; idx < inds.shape(0); ++idx) {
    result.merge(sketches_[idx]);
  }
  return result;
}

// Number of updates for each sketch
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::get_n() const -> array1d<uint64_t> {
  std::vector<uint64_t> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].get_n();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<uint64_t>(vals.data(), 1, shape);
}

// Number of retained values for each sketch
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::get_num_retained() const -> array1d<uint32_t> {
  std::vector<uint32_t> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].get_num_retained();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<uint32_t>(vals.data(), 1, shape);
}

// Gets the minimum value of each sketch
// TODO: allow subsets of sketches
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::get_min_values() const -> array1d<T> {
  std::vector<T> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].get_min_item();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<T>(vals.data(), 1, shape);
}

// Gets the maximum value of each sketch
// TODO: allow subsets of sketches
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::get_max_values() const -> array1d<T> {
  std::vector<T> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].get_max_item();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<T>(vals.data(), 1, shape);
}

// Summary of each sketch as one long string
// Users should use .split('\n\n') when calling it to build a list of each 
// sketch's summary
template<typename T, typename C>
std::string vector_of_kll_sketches<T, C>::to_string(bool print_levels, bool print_items) const {
  std::ostringstream ss;
  for (uint32_t i = 0; i < d_; ++i) {
    // all streams into 1 string, for compatibility with Python's str() behavior
    // users will need to split by \n\n, e.g., str(kll).split('\n\n')
    if (i > 0) ss << "\n";
    ss << sketches_[i].to_string(print_levels, print_items);
  }
  return ss.str();
}

template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::is_estimation_mode() const -> array1d<bool> {
  std::vector<char> vals(d_);
  for (uint32_t i = 0; i < d_; ++i) {
    vals[i] = sketches_[i].is_estimation_mode();
  }
  const size_t shape[] = { static_cast<size_t>(d_) };
  return array1d<bool>(vals.data(), 1, shape);
}

// Value of sketch(es) corresponding to some quantile(s)
template<typename T, typename C>
auto vector_of_kll_sketches<T, C>::get_quantiles(nb::ndarray<double>& ranks,
                                                 nb::ndarray<int>& isk) const -> array2d<T> {
  std::vector<uint32_t> inds = get_indices(isk);
  size_t num_sketches = inds.size();
  size_t num_quantiles = ranks.size();

  std::vector<std::vector<T>> quants(num_sketches, std::vector<T>(num_quantiles));
  for (uint32_t i = 0; i < num_sketches; ++i) {
    for (size_t j = 0; j < num_quantiles; ++j) {
      quants[i][j] = sketches_[inds[i]].get_quantile(ranks.data()[j]);
    }
  }

  const size_t shape[] = {static_cast<size_t>(num_sketches), static_cast<size_t>(num_quantiles)};
  return array2d<T>(quants, 2, shape);
}

// Value of sketch(es) corresponding to some rank(s)
template<typename T, typename C>
nb::ndarray<double, nb::ndim<2>> vector_of_kll_sketches<T, C>::get_ranks(nb::ndarray<T>& values,
                                                                         nb::ndarray<int>& isk) const {
  std::vector<uint32_t> inds = get_indices(isk);
  size_t num_sketches = inds.size();
  size_t num_ranks = values.size();
  auto vals = values.data();

  std::vector<std::vector<float>> ranks(num_sketches, std::vector<float>(num_ranks));
  for (uint32_t i = 0; i < num_sketches; ++i) {
    for (size_t j = 0; j < num_ranks; ++j) {
      ranks[i][j] = sketches_[inds[i]].get_rank(vals[j]);
    }
  }

  return nb::cast<nb::ndarray<double>, nb::ndim<2>>(ranks);
}

// PMF(s) of sketch(es)
template<typename T, typename C>
nb::ndarray<double, nb::ndim<2>> vector_of_kll_sketches<T, C>::get_pmf(nb::ndarray<T>& split_points,
                                                                       nb::ndarray<int>& isk) const {
  std::vector<uint32_t> inds = get_indices(isk);
  size_t num_sketches = inds.size();
  size_t num_splits = split_points.size();
  
  std::vector<std::vector<T>> pmfs(num_sketches, std::vector<T>(num_splits + 1));
  for (uint32_t i = 0; i < num_sketches; ++i) {
    auto pmf = sketches_[inds[i]].get_PMF(split_points.data(), num_splits);
    for (size_t j = 0; j <= num_splits; ++j) {
      pmfs[i][j] = pmf[j];
    }
  }

  return nb::cast<nb::ndarray<double, nb::ndim<2>>, nb::ndim<1>>(pmfs);
}

// CDF(s) of sketch(es)
template<typename T, typename C>
nb::ndarray<double, nb::ndim<2>> vector_of_kll_sketches<T, C>::get_cdf(nb::ndarray<T>& split_points,
                                                                       nb::ndarray<int>& isk) const {
  std::vector<uint32_t> inds = get_indices(isk);
  size_t num_sketches = inds.size();
  size_t num_splits = split_points.size();
  
  std::vector<std::vector<T>> cdfs(num_sketches, std::vector<T>(num_splits + 1));
  for (uint32_t i = 0; i < num_sketches; ++i) {
    auto cdf = sketches_[inds[i]].get_CDF(split_points.data(), num_splits);
    for (size_t j = 0; j <= num_splits; ++j) {
      cdfs[i][j] = cdf[j];
    }
  }

  return nb::cast<nb::ndarray<double>, nb::ndim<2>>(cdfs);
}

template<typename T, typename C>
void vector_of_kll_sketches<T, C>::deserialize(const nb::bytes& sk_bytes,
                                               uint32_t idx) {
  if (idx >= d_) {
    throw std::invalid_argument("request for invalid dimensions >= d ("
             + std::to_string(d_) +"): "+ std::to_string(idx));
  }
  // load the sketch into the proper index
  sketches_[idx] = std::move(kll_sketch<T>::deserialize(sk_bytes.c_str(), skStr.length()));
}

template<typename T, typename C>
nb::list vector_of_kll_sketches<T, C>::serialize(nb::ndarray<int>& isk) {
  std::vector<uint32_t> inds = get_indices(isk);
  const size_t num_sketches = inds.size();

  nb::list list;
  for (uint32_t i = 0; i < num_sketches; ++i) {
    auto serResult = sketches_[inds[i]].serialize();
    list.append(nb::bytes((char*)serResult.data(), serResult.size()));
  }

  return list;
}

} // namespace datasketches

template<typename T>
void bind_vector_of_kll_sketches(nb::module_ &m, const char* name) {
  using namespace datasketches;

  nb::class_<vector_of_kll_sketches<T>>(m, name)
    .def(nb::init<uint32_t, uint32_t>(), nb::arg("k")=vector_of_kll_constants::DEFAULT_K, 
                                         nb::arg("d")=vector_of_kll_constants::DEFAULT_D)
    .def(nb::init<const vector_of_kll_sketches<T>&>())
    // allow user to retrieve k or d, in case it's instantiated w/ defaults
    .def("get_k", &vector_of_kll_sketches<T>::get_k,
         "Returns the value of `k` of the sketch(es)")
    .def("get_d", &vector_of_kll_sketches<T>::get_d,
         "Returns the number of sketches")
    .def("update", &vector_of_kll_sketches<T>::update, nb::arg("items"), 
         "Updates the sketch(es) with value(s).  Must be a 1D array of size equal to the number of sketches.  Can also be 2D array of shape (n_updates, n_sketches).  If a sketch does not have a value to update, use np.nan")
    .def("__str__", &vector_of_kll_sketches<T>::to_string, nb::arg("print_levels")=false, nb::arg("print_items")=false,
         "Produces a string summary of all sketches. Users should split the returned string by '\n\n'")
    .def("to_string", &vector_of_kll_sketches<T>::to_string, nb::arg("print_levels")=false,
                                                             nb::arg("print_items")=false,
         "Produces a string summary of all sketches. Users should split the returned string by '\n\n'")
    .def("is_empty", &vector_of_kll_sketches<T>::is_empty,
         "Returns whether the sketch(es) is(are) empty of not")
    .def("get_n", &vector_of_kll_sketches<T>::get_n, 
         "Returns the number of values seen by the sketch(es)")
    .def("get_num_retained", &vector_of_kll_sketches<T>::get_num_retained, 
         "Returns the number of values retained by the sketch(es)")
    .def("is_estimation_mode", &vector_of_kll_sketches<T>::is_estimation_mode, 
         "Returns whether the sketch(es) is(are) in estimation mode")
    .def("get_min_values", &vector_of_kll_sketches<T>::get_min_values,
         "Returns the minimum value(s) of the sketch(es)")
    .def("get_max_values", &vector_of_kll_sketches<T>::get_max_values,
         "Returns the maximum value(s) of the sketch(es)")
    .def("get_quantiles", &vector_of_kll_sketches<T>::get_quantiles, nb::arg("ranks"),
                                                                     nb::arg("isk")=-1, 
         "Returns the value(s) associated with the specified quantile(s) for the specified sketch(es). `ranks` can be a float between 0 and 1 (inclusive), or a list/array of values. `isk` specifies which sketch(es) to return the value(s) for (default: all sketches)")
    .def("get_ranks", &vector_of_kll_sketches<T>::get_ranks, nb::arg("values"), 
                                                             nb::arg("isk")=-1, 
         "Returns the value(s) associated with the specified ranks(s) for the specified sketch(es). `values` can be an int between 0 and the number of values retained, or a list/array of values. `isk` specifies which sketch(es) to return the value(s) for (default: all sketches)")
    .def("get_pmf", &vector_of_kll_sketches<T>::get_pmf, nb::arg("split_points"), nb::arg("isk")=-1, 
         "Returns the probability mass function (PMF) at `split_points` of the specified sketch(es).  `split_points` should be a list/array of floats between 0 and 1 (inclusive). `isk` specifies which sketch(es) to return the PMF for (default: all sketches)")
    .def("get_cdf", &vector_of_kll_sketches<T>::get_cdf, nb::arg("split_points"), nb::arg("isk")=-1, 
         "Returns the cumulative distribution function (CDF) at `split_points` of the specified sketch(es).  `split_points` should be a list/array of floats between 0 and 1 (inclusive). `isk` specifies which sketch(es) to return the CDF for (default: all sketches)")
    .def_static("get_normalized_rank_error",
        [](uint16_t k, bool pmf) { return kll_sketch<T>::get_normalized_rank_error(k, pmf); },
         nb::arg("k"), nb::arg("as_pmf"), "Returns the normalized rank error")
    .def("serialize", &vector_of_kll_sketches<T>::serialize, nb::arg("isk")=-1, 
         "Serializes the specified sketch(es). `isk` can be an int or a list/array of ints (default: all sketches)")
    .def("deserialize", &vector_of_kll_sketches<T>::deserialize, nb::arg("skBytes"), nb::arg("isk"), 
         "Deserializes the specified sketch.  `isk` must be an int.")
    .def("merge", &vector_of_kll_sketches<T>::merge, nb::arg("array_of_sketches"),
         "Merges the input array of KLL sketches into the existing array.")
    .def("collapse", &vector_of_kll_sketches<T>::collapse, nb::arg("isk")=-1,
         "Returns the result of collapsing all sketches in the array into a single sketch.  'isk' can be an int or a list/array of ints (default: all sketches)")
    ;
}

void init_vector_of_kll(nb::module_ &m) {
  //bind_vector_of_kll_sketches<int>(m, "vector_of_kll_ints_sketches");
  bind_vector_of_kll_sketches<float>(m, "vector_of_kll_floats_sketches");
}
