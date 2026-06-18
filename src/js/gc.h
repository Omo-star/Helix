#pragma once
#include "js/value.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

class GC {
public:
    // Allocate a new GcCell-derived object and register it in the heap.
    template<typename T, typename... Args>
    T* alloc(Args&&... args) {
        auto* p = new T(std::forward<Args>(args)...);
        p->gcNext = m_head;
        m_head    = p;
        m_allocCount++;
        if (m_allocCount >= m_gcThreshold) collect();
        return p;
    }

    // Intern a string (returns existing JsString if value matches).
    JsString* internString(const std::string& s);
    JsString* internString(std::string&& s);

    // Create a new JsObject (allocated + registered).
    JsObject* newObject(ObjKind kind = ObjKind::Plain);
    JsObject* newArray();
    JsObject* newFunction();
    JsObject* newNativeFunction(NativeFn fn, const std::string& name, int arity = 0);
    JsObject* newError(const std::string& type, const std::string& msg);
    JsObject* newPromise();

    // Mark a JsValue as reachable (called during mark phase).
    void markValue(const JsValue& v);
    void markObject(JsObject* o);
    void markString(JsString* s);

    // Add roots that are always reachable.
    void addRoot(JsValue* p) { m_roots.push_back(p); }
    void removeRoot(JsValue* p);

    // Run a full mark-sweep cycle.
    void collect();

    // Called by VM to provide frame register ranges for marking.
    // These are temporary roots for the duration of execution.
    void pushTempRoots(JsValue* base, size_t count) {
        m_tempRoots.push_back({base, count});
    }
    void popTempRoots() {
        if (!m_tempRoots.empty()) m_tempRoots.pop_back();
    }

    ~GC();

private:
    GcCell* m_head        = nullptr;
    size_t  m_allocCount  = 0;
    size_t  m_gcThreshold = 4096;

    std::unordered_map<std::string, JsString*> m_strings; // interning table
    std::vector<JsValue*>                      m_roots;
    struct TempRange { JsValue* base; size_t count; };
    std::vector<TempRange>                     m_tempRoots;
};
