#include "digitor/native_node_qualification.hpp"
#include <map>
#include <mutex>
#include <utility>
namespace digitor {
struct NativeNodeQualificationRegistry::Impl { mutable std::mutex mutex; std::map<std::pair<int,int>,NativeNodeQualificationRecord> records; };
NativeNodeQualificationRegistry::NativeNodeQualificationRegistry():impl_(new Impl){}
NativeNodeQualificationRegistry::~NativeNodeQualificationRegistry(){delete impl_;}
bool validate_native_node_qualification(const NativeNodeQualificationRecord& r) noexcept {
 if(r.backend==DIGITOR_RENDERER_CPU||r.contract_hash==0) return false;
 if(r.state==NativeNodeQualificationState::qualified&&r.evidence.empty()) return false;
 return true;
}
void NativeNodeQualificationRegistry::record(NativeNodeQualificationRecord r){if(!validate_native_node_qualification(r))return;std::lock_guard lock(impl_->mutex);impl_->records[{(int)r.backend,(int)r.kernel}]=std::move(r);}
NativeNodeQualificationRecord NativeNodeQualificationRegistry::query(DigitorRendererBackend b,NativeNodeKernel k) const {std::lock_guard lock(impl_->mutex);auto it=impl_->records.find({(int)b,(int)k});return it==impl_->records.end()?NativeNodeQualificationRecord{b,k,NativeNodeQualificationState::unavailable,0,{}}:it->second;}
bool NativeNodeQualificationRegistry::production_ready(DigitorRendererBackend b,NativeNodeKernel k,std::uint64_t h) const {auto r=query(b,k);return r.state==NativeNodeQualificationState::qualified&&r.contract_hash==h&&!r.evidence.empty();}
void NativeNodeQualificationRegistry::retire_backend(DigitorRendererBackend b) noexcept {std::lock_guard lock(impl_->mutex);for(auto it=impl_->records.begin();it!=impl_->records.end();)it=(it->first.first==(int)b)?impl_->records.erase(it):std::next(it);}
}
