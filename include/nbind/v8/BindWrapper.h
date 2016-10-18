// This file is part of nbind, copyright (C) 2014-2015 BusFaster Ltd.
// Released under the MIT license, see LICENSE.

#pragma once

#include <memory>

#if !defined(DUPLICATE_POINTERS)

	#include <unordered_map>

#endif // DUPLICATE_POINTERS

#include <v8.h>
#include <node.h>
#include <node_buffer.h>
#include <nan.h>

namespace nbind {

template <typename A, typename B>
class HashablePair : public std::pair<A, B> {

public:

	HashablePair(A a, B b) : std::pair<A, B>(a, b) {}

};

}

namespace std {

// Add hash functions for HashablePair and TypeFlags so they can be used
// as keys in std::unordered_map.

template<typename A, typename B> struct hash<nbind::HashablePair<A, B>> {
	inline size_t operator()(const nbind::HashablePair<A, B> &obj) const {
		size_t result = hash<A>()(obj.first) + 0x9e3779b9;

		result = (result << 6) + (result >> 2);
		result += hash<B>()(obj.second) + 0x9e3779b9;

		return(result);
	}
};

template<>
struct hash<nbind::TypeFlags> {
	inline size_t operator()(nbind::TypeFlags flags) const {
		return(hash<uint32_t>()(static_cast<uint32_t>(flags)));
	}
};

}

namespace nbind {

class BindWrapperBase : public node::ObjectWrap {

public:

	BindWrapperBase(void *bound, TypeFlags flags) :
		boundUnsafe(bound), flags(flags) {}

	inline TypeFlags getFlags() const { return(flags); }

	inline void *getBound(TypeFlags argFlags) {
		if(!!(flags & TypeFlags::isConst) && !(argFlags & TypeFlags::isConst)) {
			throw(std::runtime_error("Passing a const value as a non-const argument"));
		}

		return(boundUnsafe);
	}

#if !defined(DUPLICATE_POINTERS)

	static Nan::Persistent<v8::Object> *findInstance(const void *ptr, TypeFlags flags) {
		// This will insert a null pointer (to a non-existent wrapper)
		// in the map if the object pointer is not found yet.

		return(&getInstanceTbl()[HashablePair<const void *, TypeFlags>(ptr, flags)]);
	}

#endif // DUPLICATE_POINTERS

protected:

	void wrapThis(const Nan::FunctionCallbackInfo<v8::Value> &args) {

#		if !defined(DUPLICATE_POINTERS)

			addInstance(args.This());

#		endif // DUPLICATE_POINTERS

		this->Wrap(args.This());
	}

#if !defined(DUPLICATE_POINTERS)

	// If the GC wants to free the wrapper object, get rid of our reference to it.

	static void weakCallback(const Nan::WeakCallbackInfo<Nan::Persistent<v8::Object>> &data) {
		// This causes a crash. Maybe it's a reference counter that already got decremented?
		// data.GetParameter()->Reset();
	}

	// Add a mapping from a pointer to a wrapper object,
	// to re-use the same wrapper for duplicates of the same pointer.

	void addInstance(v8::Local<v8::Object> obj) {
		Nan::Persistent<v8::Object> *ref = &getInstanceTbl()[
			HashablePair<const void *, TypeFlags>(
				boundUnsafe,
				flags
			)
		];

		ref->Reset(obj);

		// Mark the reference to the wrapper weak, so it can be
		// garbage collected when no copies remain.
		// If the same pointer needs wrapping later, we can just
		// create a new wrapper again.

		ref->SetWeak(ref, weakCallback, Nan::WeakCallbackType::kParameter);
		ref->MarkIndependent();
	}

	void removeInstance() {
		// This causes a crash. Maybe it's a reference counter that already got decremented?
		// auto iter = getInstanceTbl().find(bound.get());
		// (*iter).second.Reset();

		getInstanceTbl().erase(
			HashablePair<const void *, TypeFlags>(
				boundUnsafe,
				flags
			)
		);
	}

	// This is effectively a map from C++ instance pointers
	// to weak references to JavaScript objects wrapping them.

	static std::unordered_map<
		HashablePair<const void *, TypeFlags>,
		Nan::Persistent<v8::Object>
	> &getInstanceTbl() {
		static std::unordered_map<
			HashablePair<const void *, TypeFlags>,
			Nan::Persistent<v8::Object>
		> instanceTbl;

		return(instanceTbl);
	}

#endif // DUPLICATE_POINTERS

	void *boundUnsafe;
	TypeFlags flags;

};

// BindWrapper encapsulates a C++ object created in Node.js.

template <class Bound>
class BindWrapper : public BindWrapperBase {

public:

	BindWrapper(Bound *bound, TypeFlags flags) :
		BindWrapperBase(bound, flags) {}

	BindWrapper(std::shared_ptr<Bound> bound, TypeFlags flags) :
		BindWrapperBase(bound.get(), flags), boundShared(bound) {}

	// This destructor is called automatically by the JavaScript garbage collector.

	~BindWrapper() {
		destroy();
	}

	// Pass any constructor arguments to wrapped class.
	template<typename... Args>
	static void createObj(
		const Nan::FunctionCallbackInfo<v8::Value> &nanArgs,
		Args&&... args
	) {
		(new BindWrapper(
			std::make_shared<Bound>(args...),
			TypeFlags::isSharedPtr
		))->wrapThis(nanArgs);
	}

	static void wrapPtr(const Nan::FunctionCallbackInfo<v8::Value> &nanArgs) {
		auto flags = static_cast<TypeFlags>(nanArgs[1]->Uint32Value());
		void *ptr = v8::Handle<v8::External>::Cast(nanArgs[0])->Value();

		if((flags & TypeFlags::refMask) == TypeFlags::isSharedPtr) {
			auto *ptrShared = static_cast<std::shared_ptr<Bound> *>(ptr);

			(new BindWrapper(*ptrShared, flags))->wrapThis(nanArgs);

			// Delete temporary shared pointer after re-referencing target object.
			delete ptrShared;
		} else {
			auto *ptrUnsafe = static_cast<Bound *>(ptr);

			(new BindWrapper(ptrUnsafe, flags))->wrapThis(nanArgs);
		}
	}

	void destroy() {

		// Avoid freeing the object twice.
		if(!boundUnsafe) return;

#		if !defined(DUPLICATE_POINTERS)

			// JavaScript side no longer holds any references to the object,
			// so remove our weak pointer to the wrapper.

			removeInstance();

#		endif // DUPLICATE_POINTERS

		// Delete the bound object if the C++ side isn't holding onto it.
		// The weak pointer must be removed first,
		// because resetting changes the hash key.

		boundUnsafe = nullptr;
		boundShared.reset();

	}

	// TODO: this should throw or never get called if bound is not shared!

	inline std::shared_ptr<Bound> getShared() { return(boundShared); }

private:

	std::shared_ptr<Bound> boundShared;

};

} // namespace
