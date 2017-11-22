/*
 * (C) Copyright 1996-2017 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */

#pragma once

#include "atlas/util/Config.h"
#include "atlas/array/DataType.h"

// ----------------------------------------------------------------------------

namespace atlas {
namespace option {

// ----------------------------------------------------------------------------

class type : public util::Config {
public:
  type( const std::string& );
};

// ----------------------------------------------------------------------------

class global : public util::Config {
public:
  global( size_t owner = 0 );
};

// ----------------------------------------------------------------------------

class levels : public util::Config {
public:
  levels( size_t );
};

// ----------------------------------------------------------------------------

class variables : public util::Config {
public:
  variables( size_t );
};

// ----------------------------------------------------------------------------

class name : public util::Config {
public:
  name( const std::string& );
};

// ----------------------------------------------------------------------------

template< typename T >
class datatypeT : public util::Config {
public:
  datatypeT();
};

// ----------------------------------------------------------------------------

class datatype : public util::Config {
public:
  datatype( array::DataType::kind_t );
  datatype( const std::string& );
  datatype( array::DataType );
};

// ----------------------------------------------------------------------------

class halo : public util::Config {
public:
  halo(size_t size);
};

// ----------------------------------------------------------------------------

class radius : public util::Config {
public:
  radius( double );
  radius( const std::string& = "Earth" );
};

// ----------------------------------------------------------------------------
// Definitions
// ----------------------------------------------------------------------------

template<typename T>
datatypeT<T>::datatypeT() {
  set("datatype",array::DataType::kind<T>());
}



} // namespace option
} // namespace atlas
