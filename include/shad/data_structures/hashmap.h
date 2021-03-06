//===------------------------------------------------------------*- C++ -*-===//
//
//                                     SHAD
//
//      The Scalable High-performance Algorithms and Data Structure Library
//
//===----------------------------------------------------------------------===//
//
// Copyright 2018 Battelle Memorial Institute
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_SHAD_DATA_STRUCTURES_HASHMAP_H_
#define INCLUDE_SHAD_DATA_STRUCTURES_HASHMAP_H_

#include <algorithm>
#include <tuple>
#include <utility>
#include <vector>

#include "shad/data_structures/abstract_data_structure.h"
#include "shad/data_structures/buffer.h"
#include "shad/data_structures/compare_and_hash_utils.h"
#include "shad/data_structures/local_hashmap.h"
#include "shad/runtime/runtime.h"

namespace shad {

/// @brief The Hashmap data structure.
///
/// SHAD's Hashmap is a distributed, thread-safe, associative container.
/// @tparam KTYPE type of the hashmap keys.
/// @tparam VTYPE type of the hashmap values.
/// @tparam KEY_COMPARE key comparison function; default is MemCmp<KTYPE>.
/// @warning obects of type KTYPE and VTYPE need to be trivially copiable.
/// @tparam INSERT_POLICY insertion policy; default is overwrite
/// (i.e. insertions overwrite previous values
///  associated to the same key, if any).
template <typename KTYPE, typename VTYPE, typename KEY_COMPARE = MemCmp<KTYPE>,
          typename INSERT_POLICY = Overwriter<VTYPE>>
class Hashmap : public AbstractDataStructure<
                    Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>> {
  template <typename>
  friend class AbstractDataStructure;

 public:
  using HmapT = Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>;
  using LMapT = LocalHashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>;
  using ObjectID = typename AbstractDataStructure<HmapT>::ObjectID;
  using ShadHashmapPtr = typename AbstractDataStructure<HmapT>::SharedPtr;
  struct EntryT {
    EntryT(const KTYPE &k, const VTYPE &v) : key(k), value(v) {}
    EntryT() = default;
    KTYPE key;
    VTYPE value;
  };
  using BuffersVector = typename impl::BuffersVector<EntryT, HmapT>;

  /// @brief Create method.
  ///
  /// Creates a newhashmap instance.
  /// @param numEntries Expected number of entries.
  /// @return A shared pointer to the newly created hashmap instance.
#ifdef DOXYGEN_IS_RUNNING
  static ShadHashmapPtr Create(const size_t numEntries);
#endif

  /// @brief Getter of the Global Identifier.
  ///
  /// @return The global identifier associated with the hashmap instance.
  ObjectID GetGlobalID() const { return oid_; }

  /// @brief Overall size of the hashmap (number of entries).
  /// @warning Calling the size method may result in one-to-all
  /// communication among localities to retrieve consinstent information.
  /// @return the size of the hashmap.
  size_t Size() const;

  /// @brief Insert a key-value pair in the hashmap.
  /// @param[in] key the key.
  /// @param[in] value the value to copy into the hashmap.
  void Insert(const KTYPE &key, const VTYPE &value);

  /// @brief Asynchronously Insert a key-value pair in the hashmap.
  /// @warning Asynchronous operations are guaranteed to have completed
  /// only after calling the rt::waitForCompletion(rt::Handle &handle) method.
  /// @param[in,out] handle Reference to the handle
  /// to be used to wait for completion.
  /// @param[in] key the key.
  /// @param[in] value the value to copy into the hashMap.
  /// @return a pointer to the value if the the key-value was inserted
  ///        or a pointer to a previously inserted value.
  void AsyncInsert(rt::Handle &handle, const KTYPE &key, const VTYPE &value);

  /// @brief Buffered Insert method.
  /// Inserts a key-value pair, using aggregation buffers.
  /// @warning Insertions are finalized only after calling
  /// the WaitForBufferedInsert() method.
  /// @param[in] key The key.
  /// @param[in] value The value.
  void BufferedInsert(const KTYPE &key, const VTYPE &value);

  /// @brief Asynchronous Buffered Insert method.
  /// Asynchronously inserts a key-value pair, using aggregation buffers.
  /// @warning asynchronous buffered insertions are finalized only after
  /// calling the rt::waitForCompletion(rt::Handle &handle) method AND
  /// the WaitForBufferedInsert() method, in this order.
  /// @param[in,out] handle Reference to the handle
  /// @param[in] key The key.
  /// @param[in] value The value.
  void BufferedAsyncInsert(rt::Handle &handle, const KTYPE &key,
                           const VTYPE &value);

  /// @brief Finalize method for buffered insertions.
  void WaitForBufferedInsert() {
    auto flushLambda_ = [](const ObjectID &oid) {
      auto ptr = HmapT::GetPtr(oid);
      ptr->buffers_.FlushAll();
    };
    rt::executeOnAll(flushLambda_, oid_);
  }
  /// @brief Remove a key-value pair from the hashmap.
  /// @param[in] key the key.
  void Erase(const KTYPE &key);

  /// @brief Asynchronously remove a key-value pair from the hashmap.
  /// @warning Asynchronous operations are guaranteed to have completed
  /// only after calling the rt::waitForCompletion(rt::Handle &handle) method.
  /// @param[in,out] handle Reference to the handle
  /// to be used to wait for completion.
  /// @param[in] key the key.
  void AsyncErase(rt::Handle &handle, const KTYPE &key);

  /// @brief Clear the content of the hashmap.
  void Clear() {
    auto clearLambda = [](const ObjectID &oid) {
      auto mapPtr = HmapT::GetPtr(oid);
      mapPtr->localMap_.Clear();
    };
    rt::executeOnAll(clearLambda, oid_);
  }

  using LookupResult =
      typename LocalHashmap<KTYPE, VTYPE, KEY_COMPARE>::LookupResult;

  /// @brief Get the value associated to a key.
  /// @param[in] key the key.
  /// @param[out] res a pointer to the value if the the key-value was found
  ///             and NULL if it does not exists.
  /// @return true if the entry is found, false otherwise.
  bool Lookup(const KTYPE &key, VTYPE *res);

  /// @brief Asynchronous lookup method.
  /// @warning Asynchronous operations are guaranteed to have completed.
  /// only after calling the rt::waitForCompletion(rt::Handle &handle) method.
  /// @param[in,out] handle Reference to the handle.
  /// to be used to wait for completion.
  /// @param[in] key The key.
  /// @param[out] res The result of the lookup operation.
  void AsyncLookup(rt::Handle &handle, const KTYPE &key, LookupResult *res);

  /// @brief Apply a user-defined function to a key-value pair.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(const KTYPE&, VTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param key The key.
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void Apply(const KTYPE &key, ApplyFunT &&function, Args &... args);

  /// @brief Asynchronously apply a user-defined function to a key-value pair.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(rt::Handle &handle, const KTYPE&, VTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param[in,out] handle Reference to the handle.
  /// @param key The key.
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void AsyncApply(rt::Handle &handle, const KTYPE &key, ApplyFunT &&function,
                  Args &... args);

  /// @brief Apply a user-defined function to each key-value pair.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(const KTYPE&, VTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void ForEachEntry(ApplyFunT &&function, Args &... args);

  /// @brief Asynchronously apply a user-defined function to each key-value
  /// pair.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(shad::rt::Handle&, const KTYPE&, VTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param[in,out] handle Reference to the handle.
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void AsyncForEachEntry(rt::Handle &handle, ApplyFunT &&function,
                         Args &... args);

  /// @brief Apply a user-defined function to each key.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(const KTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void ForEachKey(ApplyFunT &&function, Args &... args);

  /// @brief Asynchronously apply a user-defined function to each key.
  ///
  /// @tparam ApplyFunT User-defined function type.  The function prototype
  /// should be:
  /// @code
  /// void(shad::rt::Handle&, const KTYPE&, Args&);
  /// @endcode
  /// @tparam ...Args Types of the function arguments.
  ///
  /// @param[in,out] handle Reference to the handle.
  /// @param function The function to apply.
  /// @param args The function arguments.
  template <typename ApplyFunT, typename... Args>
  void AsyncForEachKey(rt::Handle &handle, ApplyFunT &&function,
                       Args &... args);

  void PrintAllEntries() {
    auto printLambda = [](const ObjectID &oid) {
      auto mapPtr = HmapT::GetPtr(oid);
      std::cout << "---- Locality: " << rt::thisLocality() << std::endl;
      mapPtr->localMap_.PrintAllEntries();
    };
    rt::executeOnAll(printLambda, oid_);
  }

  // FIXME it should be protected
  void BufferEntryInsert(const EntryT &entry) {
    localMap_.Insert(entry.key, entry.value);
  }

 private:
  ObjectID oid_;
  LocalHashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY> localMap_;
  BuffersVector buffers_;

  struct InsertArgs {
    ObjectID oid;
    KTYPE key;
    VTYPE value;
  };

  struct LookupArgs {
    ObjectID oid;
    KTYPE key;
  };

 protected:
  Hashmap(ObjectID oid, const size_t numEntries)
      : oid_(oid),
        localMap_(
            std::max(numEntries / constants::kDefaultNumEntriesPerBucket, 1lu)),
        buffers_(oid) {}
};

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline size_t Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::Size() const {
  size_t size = localMap_.size_;
  size_t remoteSize;
  auto sizeLambda = [](const ObjectID &oid, size_t *res) {
    auto mapPtr = HmapT::GetPtr(oid);
    *res = mapPtr->localMap_.size_;
  };
  for (auto tgtLoc : rt::allLocalities()) {
    if (tgtLoc != rt::thisLocality()) {
      rt::executeAtWithRet(tgtLoc, sizeLambda, oid_, &remoteSize);
      size += remoteSize;
    }
  }
  return size;
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::Insert(
    const KTYPE &key, const VTYPE &value) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.Insert(key, value);
  } else {
    auto insertLambda = [](const InsertArgs &args) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      mapPtr->localMap_.Insert(args.key, args.value);
    };
    InsertArgs args = {oid_, key, value};
    rt::executeAt(targetLocality, insertLambda, args);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncInsert(
    rt::Handle &handle, const KTYPE &key, const VTYPE &value) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.AsyncInsert(handle, key, value);
  } else {
    auto insertLambda = [](rt::Handle &handle, const InsertArgs &args) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      mapPtr->localMap_.AsyncInsert(handle, args.key, args.value);
    };
    InsertArgs args = {oid_, key, value};
    rt::asyncExecuteAt(handle, targetLocality, insertLambda, args);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::BufferedInsert(
    const KTYPE &key, const VTYPE &value) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);
  if (targetLocality == rt::thisLocality()) {
    localMap_.Insert(key, value);
  } else {
    buffers_.Insert(EntryT(key, value), targetLocality);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE,
                    INSERT_POLICY>::BufferedAsyncInsert(rt::Handle &handle,
                                                        const KTYPE &key,
                                                        const VTYPE &value) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);
  if (targetLocality == rt::thisLocality()) {
    localMap_.AsyncInsert(handle, key, value);
  } else {
    EntryT entry(key, value);
    buffers_.AsyncInsert(handle, entry, targetLocality);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::Erase(
    const KTYPE &key) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.Erase(key);
  } else {
    auto eraseLambda = [](const LookupArgs &args) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      mapPtr->localMap_.Erase(args.key);
    };
    LookupArgs args = {oid_, key};
    rt::executeAt(targetLocality, eraseLambda, args);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncErase(
    rt::Handle &handle, const KTYPE &key) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.AsyncErase(handle, key);
  } else {
    auto eraseLambda = [](rt::Handle &handle, const LookupArgs &args) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      mapPtr->localMap_.AsyncErase(handle, args.key);
    };
    LookupArgs args = {oid_, key};
    rt::asyncExecuteAt(handle, targetLocality, eraseLambda, args);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline bool Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::Lookup(
    const KTYPE &key, VTYPE *res) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    return localMap_.Lookup(key, res);
  } else {
    auto lookupLambda = [](const LookupArgs &args, LookupResult *res) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      res->found = mapPtr->localMap_.Lookup(args.key, &res->value);
    };
    LookupArgs args = {oid_, key};
    LookupResult lres;
    rt::executeAtWithRet(targetLocality, lookupLambda, args, &lres);
    if (lres.found) {
      *res = std::move(lres.value);
    }
    return lres.found;
  }
  return false;
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
inline void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncLookup(
    rt::Handle &handle, const KTYPE &key, LookupResult *res) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.AsyncLookup(handle, key, res);
  } else {
    auto lookupLambda = [](rt::Handle &, const LookupArgs &args,
                           LookupResult *res) {
      auto mapPtr = HmapT::GetPtr(args.oid);
      LookupResult tres;
      mapPtr->localMap_.Lookup(args.key, &tres);
      *res = tres;
    };
    LookupArgs args = {oid_, key};
    rt::asyncExecuteAtWithRet(handle, targetLocality, lookupLambda, args, res);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::ForEachEntry(
    ApplyFunT &&function, Args &... args) {
  using FunctionTy = void (*)(const KTYPE &, VTYPE &, Args &...);
  FunctionTy fn = std::forward<decltype(function)>(function);
  using feArgs = std::tuple<ObjectID, FunctionTy, std::tuple<Args...>>;
  using LMapPtr = LocalHashmap<KTYPE, VTYPE, KEY_COMPARE> *;
  using ArgsTuple = std::tuple<LMapT *, FunctionTy, std::tuple<Args...>>;
  feArgs arguments(oid_, fn, std::tuple<Args...>(args...));
  auto feLambda = [](const feArgs &args) {
    auto mapPtr = HmapT::GetPtr(std::get<0>(args));
    ArgsTuple argsTuple(&mapPtr->localMap_, std::get<1>(args),
                        std::get<2>(args));
    rt::forEachAt(rt::thisLocality(),
                  LMapT::template ForEachEntryFunWrapper<ArgsTuple, Args...>,
                  argsTuple, mapPtr->localMap_.numBuckets_);
  };
  rt::executeOnAll(feLambda, arguments);
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncForEachEntry(
    rt::Handle &handle, ApplyFunT &&function, Args &... args) {
  using FunctionTy = void (*)(rt::Handle &, const KTYPE &, VTYPE &, Args &...);
  FunctionTy fn = std::forward<decltype(function)>(function);
  using feArgs = std::tuple<ObjectID, FunctionTy, std::tuple<Args...>>;
  using ArgsTuple = std::tuple<LMapT *, FunctionTy, std::tuple<Args...>>;
  feArgs arguments(oid_, fn, std::tuple<Args...>(args...));
  auto feLambda = [](rt::Handle &handle, const feArgs &args) {
    auto mapPtr = HmapT::GetPtr(std::get<0>(args));
    ArgsTuple argsTuple(&mapPtr->localMap_, std::get<1>(args),
                        std::get<2>(args));
    rt::asyncForEachAt(
        handle, rt::thisLocality(),
        LMapT::template AsyncForEachEntryFunWrapper<ArgsTuple, Args...>,
        argsTuple, mapPtr->localMap_.numBuckets_);
  };
  rt::asyncExecuteOnAll(handle, feLambda, arguments);
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::ForEachKey(
    ApplyFunT &&function, Args &... args) {
  using FunctionTy = void (*)(const KTYPE &, Args &...);
  FunctionTy fn = std::forward<decltype(function)>(function);
  using feArgs = std::tuple<ObjectID, FunctionTy, std::tuple<Args...>>;
  using ArgsTuple = std::tuple<LMapT *, FunctionTy, std::tuple<Args...>>;
  feArgs arguments(oid_, fn, std::tuple<Args...>(args...));
  auto feLambda = [](const feArgs &args) {
    auto mapPtr = HmapT::GetPtr(std::get<0>(args));
    ArgsTuple argsTuple(&mapPtr->localMap_, std::get<1>(args),
                        std::get<2>(args));
    rt::forEachAt(rt::thisLocality(),
                  LMapT::template ForEachKeyFunWrapper<ArgsTuple, Args...>,
                  argsTuple, mapPtr->localMap_.numBuckets_);
  };
  rt::executeOnAll(feLambda, arguments);
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncForEachKey(
    rt::Handle &handle, ApplyFunT &&function, Args &... args) {
  using FunctionTy = void (*)(rt::Handle &, const KTYPE &, Args &...);
  FunctionTy fn = std::forward<decltype(function)>(function);
  using feArgs = std::tuple<ObjectID, FunctionTy, std::tuple<Args...>>;
  using ArgsTuple = std::tuple<LMapT *, FunctionTy, std::tuple<Args...>>;
  feArgs arguments(oid_, fn, std::tuple<Args...>(args...));
  auto feLambda = [](rt::Handle &handle, const feArgs &args) {
    auto mapPtr = HmapT::GetPtr(std::get<0>(args));
    ArgsTuple argsTuple(&mapPtr->localMap_, std::get<1>(args),
                        std::get<2>(args));
    rt::asyncForEachAt(
        handle, rt::thisLocality(),
        LMapT::template AsyncForEachKeyFunWrapper<ArgsTuple, Args...>,
        argsTuple, mapPtr->localMap_.numBuckets_);
  };
  rt::asyncExecuteOnAll(handle, feLambda, arguments);
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::Apply(
    const KTYPE &key, ApplyFunT &&function, Args &... args) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);
  if (targetLocality == rt::thisLocality()) {
    localMap_.Apply(key, function, args...);
  } else {
    using FunctionTy = void (*)(const KTYPE &, VTYPE &, Args &...);
    FunctionTy fn = std::forward<decltype(function)>(function);
    using ArgsTuple =
        std::tuple<ObjectID, const KTYPE, FunctionTy, std::tuple<Args...>>;
    ArgsTuple arguments(oid_, key, fn, std::tuple<Args...>(args...));
    auto feLambda = [](const ArgsTuple &args) {
      constexpr auto Size = std::tuple_size<
          typename std::decay<decltype(std::get<3>(args))>::type>::value;
      ArgsTuple &tuple = const_cast<ArgsTuple &>(args);
      LMapT *mapPtr = &(HmapT::GetPtr(std::get<0>(tuple))->localMap_);
      LMapT::CallApplyFun(mapPtr, std::get<1>(tuple), std::get<2>(tuple),
                          std::get<3>(tuple), std::make_index_sequence<Size>{});
    };
    rt::executeAt(targetLocality, feLambda, arguments);
  }
}

template <typename KTYPE, typename VTYPE, typename KEY_COMPARE,
          typename INSERT_POLICY>
template <typename ApplyFunT, typename... Args>
void Hashmap<KTYPE, VTYPE, KEY_COMPARE, INSERT_POLICY>::AsyncApply(
    rt::Handle &handle, const KTYPE &key, ApplyFunT &&function,
    Args &... args) {
  uint64_t targetId = HashFunction<KTYPE>(key, 0) % rt::numLocalities();
  rt::Locality targetLocality(targetId);

  if (targetLocality == rt::thisLocality()) {
    localMap_.AsyncApply(handle, key, function, args...);
  } else {
    using FunctionTy =
        void (*)(rt::Handle &, const KTYPE &, VTYPE &, Args &...);
    FunctionTy fn = std::forward<decltype(function)>(function);
    using ArgsTuple =
        std::tuple<ObjectID, const KTYPE, FunctionTy, std::tuple<Args...>>;
    ArgsTuple arguments(oid_, key, fn, std::tuple<Args...>(args...));
    auto feLambda = [](rt::Handle &handle, const ArgsTuple &args) {
      constexpr auto Size = std::tuple_size<
          typename std::decay<decltype(std::get<3>(args))>::type>::value;
      ArgsTuple &tuple(const_cast<ArgsTuple &>(args));
      LMapT *mapPtr = &(HmapT::GetPtr(std::get<0>(tuple))->localMap_);
      LMapT::AsyncCallApplyFun(handle, mapPtr, std::get<1>(tuple),
                               std::get<2>(tuple), std::get<3>(tuple),
                               std::make_index_sequence<Size>{});
    };
    rt::asyncExecuteAt(handle, targetLocality, feLambda, arguments);
  }
}

}  // namespace shad

#endif  // INCLUDE_SHAD_DATA_STRUCTURES_HASHMAP_H_
