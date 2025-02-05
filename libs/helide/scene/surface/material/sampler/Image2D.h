// Copyright 2022 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Sampler.h"
#include "array/Array2D.h"

namespace helide {

struct Image2D : public Sampler
{
  Image2D(HelideGlobalState *d);

  bool isValid() const override;
  void commit() override;

  float4 getSample(const Geometry &g, const Ray &r) const override;

 private:
  helium::IntrusivePtr<Array2D> m_image;
  Attribute m_inAttribute{Attribute::NONE};
  WrapMode m_wrapMode1{WrapMode::DEFAULT};
  WrapMode m_wrapMode2{WrapMode::DEFAULT};
  bool m_linearFilter{true};
  mat4 m_inTransform{mat4(linalg::identity)};
  mat4 m_outTransform{mat4(linalg::identity)};
};

} // namespace helide
