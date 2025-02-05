// Copyright 2023 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Buffer.h"
#include "async/connection.h"
#include "async/connection_manager.h"
#include "async/work_queue.h"
#include <anari/backend/DeviceImpl.h>
#include "utility/IntrusivePtr.h"
#include "utility/ParameterizedObject.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>
#include "Frame.h"
#include "Compression.h"

namespace remote {

struct Device : anari::DeviceImpl, helium::ParameterizedObject
{
  //--- Data Arrays ---------------------------------

  void* mapParameterArray1D(ANARIObject o,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t *elementStride) override;
  void* mapParameterArray2D(ANARIObject o,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t *elementStride) override;
  void* mapParameterArray3D(ANARIObject o,
      const char* name,
      ANARIDataType dataType,
      uint64_t numElements1,
      uint64_t numElements2,
      uint64_t numElements3,
      uint64_t *elementStride) override;
  void unmapParameterArray(ANARIObject o,
      const char* name) override;

  ANARIArray1D newArray1D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1) override;

  ANARIArray2D newArray2D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2) override;

  ANARIArray3D newArray3D(const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      ANARIDataType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3) override;

  void *mapArray(ANARIArray) override;

  void unmapArray(ANARIArray) override;

  //--- Renderable Objects --------------------------

  ANARILight newLight(const char *type) override;

  ANARICamera newCamera(const char *type) override;

  ANARIGeometry newGeometry(const char *type) override;

  ANARISpatialField newSpatialField(const char *type) override;

  ANARISurface newSurface() override;

  ANARIVolume newVolume(const char *type) override;

  //--- Surface Meta-Data ---------------------------

  ANARIMaterial newMaterial(const char *material_type) override;

  ANARISampler newSampler(const char *type) override;

  //--- Instancing ----------------------------------

  ANARIGroup newGroup() override;

  ANARIInstance newInstance() override;

  //--- Top-level Worlds ----------------------------

  ANARIWorld newWorld() override;

  //--- Object + Parameter Lifetime Management ------

  void setParameter(ANARIObject object,
      const char *name,
      ANARIDataType type,
      const void *mem) override;

  void unsetParameter(ANARIObject object, const char *name) override;

  void commitParameters(ANARIObject object) override;

  void release(ANARIObject _obj) override;

  void retain(ANARIObject _obj) override;

  //--- Object Query Interface ----------------------

  int getProperty(ANARIObject object,
      const char *name,
      ANARIDataType type,
      void *mem,
      uint64_t size,
      ANARIWaitMask mask) override;

  const char ** getObjectSubtypes(ANARIDataType objectType) override;
  const void* getObjectInfo(ANARIDataType objectType,
      const char* objectSubtype,
      const char* infoName,
      ANARIDataType infoType) override;
  const void* getParameterInfo(ANARIDataType objectType,
      const char* objectSubtype,
      const char* parameterName,
      ANARIDataType parameterType,
      const char* infoName,
      ANARIDataType infoType) override;

  //--- FrameBuffer Manipulation --------------------

  ANARIFrame newFrame() override;

  const void *frameBufferMap(ANARIFrame fb,
      const char *channel,
      uint32_t *width,
      uint32_t *height,
      ANARIDataType *pixelType) override;

  void frameBufferUnmap(ANARIFrame fb, const char *channel) override;

  //--- Frame Rendering -----------------------------

  ANARIRenderer newRenderer(const char *type) override;

  void renderFrame(ANARIFrame) override;

  int frameReady(ANARIFrame, ANARIWaitMask) override;

  void discardFrame(ANARIFrame) override;

 public:
  Device(std::string subtype = "default");
  ~Device();

 private:
  void initClient();
  uint64_t nextObjectID = 1;

  struct {
    std::string hostname = "localhost";
    unsigned short port{31050};
    CompressionFeatures compression;
  } server;

  async::connection_manager_pointer manager;
  async::connection_pointer conn;
  async::work_queue queue;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncConnectionEstablished;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncDeviceHandleRemote;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncMapArray;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncUnmapArray;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncFrameIsReady;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
    size_t which;
  } syncProperties;

  struct
  {
    std::mutex mtx;
    std::condition_variable cv;
  } syncObjectSubtypes;

  ANARIDevice remoteDevice{nullptr};
  std::string remoteSubtype = "default";

  // getProperty blocks until property.object is
  // not null, then consumes the remaining values
  struct
  {
    ANARIObject object{nullptr};
    std::string name;
    std::vector<char> mem;
    ANARIDataType type;
    uint64_t size;
    int result;
  } property;

  // Cache string list properties and keep strings
  // alive for the device's lifetime!
  struct StringListProperty
  {
    ANARIObject object{nullptr};
    std::string name;
    std::vector<char *> value;
  };
  std::vector<StringListProperty> stringListProperties;

  // Store subtypes; if the subtype was already queried,
  // simply return the cached subtype list
  struct ObjectSubtypes
  {
    ANARIDataType objectType;
    std::vector<char *> value;
  };
  std::vector<ObjectSubtypes> objectSubtypes;

  std::map<ANARIObject, Frame> frames;
  std::map<ANARIArray, std::vector<char>> arrays;

  // Need to keep track of these to implement
  // (un)mapParameterArray correctly
  struct ParameterArray
  {
    ANARIObject object{nullptr};
    const char *name = "";
    bool operator<(const ParameterArray &other) const {
      return object && object == other.object
        && strlen(name) > 0
        && std::string(name) < std::string(other.name);
      }
  };
  std::map<ParameterArray, ANARIArray> parameterArrays;

  ANARIObject registerNewObject(ANARIDataType type, std::string subtype = "");
  ANARIArray registerNewArray(ANARIDataType type,
      const void *appMemory,
      ANARIMemoryDeleter deleter,
      const void *userdata,
      const ANARIDataType elementType,
      uint64_t numItems1,
      uint64_t numItems2,
      uint64_t numItems3);

  //--- Net ---------------------------------------------
  void connect(std::string host, unsigned short port);

  void run();

  void write(unsigned type, std::shared_ptr<Buffer> buf);
  void write(unsigned type, const void *begin, const void *end);

  bool handleNewConnection(
      async::connection_pointer new_conn, boost::system::error_code const &e);

  void handleMessage(async::connection::reason reason,
      async::message_pointer message,
      boost::system::error_code const &e);

  void writeImpl(unsigned type, std::shared_ptr<Buffer> buf);
  void writeImpl2(unsigned type, const void *begin, const void *end);

  //--- Stats -------------------------------------------
  struct
  {
    double beforeRenderFrame = 0.0;
    double afterFrameReady = 0.0;
    double beforeFrameDecoded = 0.0;
    double afterFrameDecoded = 0.0;
  } timing;
};

} // namespace remote
