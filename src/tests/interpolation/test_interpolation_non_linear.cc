/*
 * (C) Copyright 1996- ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 *
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */


#include <algorithm>
#include <limits>

#include "atlas/array.h"
#include "atlas/field/MissingValue.h"
#include "atlas/functionspace.h"
#include "atlas/grid.h"
#include "atlas/interpolation.h"
#include "atlas/interpolation/NonLinear.h"
#include "atlas/mesh.h"
#include "atlas/meshgenerator.h"
#include "atlas/runtime/Exception.h"

#include "tests/AtlasTestEnvironment.h"


namespace atlas {
namespace test {


const double missingValue    = 42.;
const double missingValueEps = 1e-9;
const double nan             = std::numeric_limits<double>::quiet_NaN();

using field::MissingValue;
using util::Config;


CASE( "MissingValue (basic)" ) {
    SECTION( "not defined" ) {
        auto mv = MissingValue();
        EXPECT( !bool( mv ) );

        mv = MissingValue( "not defined", Config() );
        EXPECT( !bool( mv ) );
    }


    SECTION( "nan" ) {
        Config config;

        auto mv = MissingValue( "nan", config );
        EXPECT( bool( mv ) );

        EXPECT( mv( nan ) );
        EXPECT( mv( missingValue ) == false );

        config.set( "type", "nan" );
        mv = MissingValue( config );
        EXPECT( bool( mv ) );

        EXPECT( mv( nan ) );
        EXPECT( mv( missingValue ) == false );
    }


    SECTION( "equals" ) {
        Config config;
        config.set( "missing_value", missingValue );

        auto mv = MissingValue( "equals", config );
        EXPECT( bool( mv ) );

        EXPECT( mv( missingValue - 1 ) == false );
        EXPECT( mv( missingValue - missingValueEps / 2 ) == false );
        EXPECT( mv( missingValue ) );
        EXPECT( mv( missingValue + missingValueEps / 2 ) == false );
        EXPECT( mv( missingValue + 1 ) == false );

        config.set( "type", "equals" );
        mv = MissingValue( config );
        EXPECT( bool( mv ) );

        EXPECT( mv( missingValue - 1 ) == false );
        EXPECT( mv( missingValue - missingValueEps / 2 ) == false );
        EXPECT( mv( missingValue ) );
        EXPECT( mv( missingValue + missingValueEps / 2 ) == false );
        EXPECT( mv( missingValue + 1 ) == false );
    }


    SECTION( "approximately-equals" ) {
        Config config;
        config.set( "missing_value", missingValue );
        config.set( "missing_value_epsilon", missingValueEps );

        auto mv = MissingValue( "approximately-equals", config );
        EXPECT( bool( mv ) );

        EXPECT( mv( missingValue - missingValueEps * 2 ) == false );
        EXPECT( mv( missingValue - missingValueEps / 2 ) );
        EXPECT( mv( missingValue ) );
        EXPECT( mv( missingValue + missingValueEps / 2 ) );
        EXPECT( mv( missingValue + missingValueEps * 2 ) == false );

        config.set( "type", "approximately-equals" );
        mv = MissingValue( config );
        EXPECT( bool( mv ) );

        EXPECT( mv( missingValue - missingValueEps * 2 ) == false );
        EXPECT( mv( missingValue - missingValueEps / 2 ) );
        EXPECT( mv( missingValue ) );
        EXPECT( mv( missingValue + missingValueEps / 2 ) );
        EXPECT( mv( missingValue + missingValueEps * 2 ) == false );
    }
}


CASE( "MissingValue (DataType specialisations)" ) {
    SECTION( "real64" ) {
        auto n   = static_cast<double>( missingValue );
        auto eps = static_cast<double>( missingValueEps );
        auto nan = std::numeric_limits<double>::quiet_NaN();

        Config config;
        config.set( "missing_value", n );
        config.set( "missing_value_epsilon", eps );

        for ( std::string type : {"nan", "equals", "approximately-equals"} ) {
            auto mv = MissingValue( type + "-real64", config );
            EXPECT( bool( mv ) );
            EXPECT( mv( type == "nan" ? nan : n ) );
            EXPECT( mv( n ) != mv( nan ) );
            EXPECT( mv( n + 1 ) == false );
        }
    }


    SECTION( "real32" ) {
        auto n   = static_cast<float>( missingValue );
        auto eps = static_cast<float>( missingValueEps );
        auto nan = std::numeric_limits<float>::quiet_NaN();

        Config config;
        config.set( "missing_value", n );
        config.set( "missing_value_epsilon", eps );

        for ( std::string type : {"nan", "equals", "approximately-equals"} ) {
            auto mv = MissingValue( type + "-real32", config );
            EXPECT( bool( mv ) );
            EXPECT( mv( type == "nan" ? nan : n ) );
            EXPECT( mv( n ) != mv( nan ) );
            EXPECT( mv( n + 1 ) == false );
        }
    }


    SECTION( "int32" ) {
        auto n  = static_cast<int>( missingValue );
        auto mv = MissingValue( "equals-int32", Config( "missing_value", n ) );
        EXPECT( bool( mv ) );
        EXPECT( mv( n ) );
        EXPECT( mv( n + 1 ) == false );
    }


    SECTION( "int64" ) {
        auto n  = static_cast<long>( missingValue );
        auto mv = MissingValue( "equals-int64", Config( "missing_value", n ) );
        EXPECT( bool( mv ) );
        EXPECT( mv( n ) );
        EXPECT( mv( n + 1 ) == false );
    }


    SECTION( "uint64" ) {
        auto n  = static_cast<unsigned long>( missingValue );
        auto mv = MissingValue( "equals-uint64", Config( "missing_value", n ) );
        EXPECT( bool( mv ) );
        EXPECT( mv( n ) );
        EXPECT( mv( n + 1 ) == false );
    }
}


CASE( "MissingValue from Field (basic)" ) {
    std::vector<double> values{1., nan, missingValue, missingValue, missingValue + missingValueEps / 2., 6., 7.};
    Field field( "field", values.data(), array::make_shape( values.size(), 1 ) );

    field.metadata().set( "missing_value_type", "not defined" );
    field.metadata().set( "missing_value", missingValue );
    field.metadata().set( "missing_value_epsilon", missingValueEps );

    EXPECT( !bool( MissingValue( field ) ) );


    SECTION( "nan" ) {
        // missing value type from user
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( "nan", field ) ) == 1 );

        // missing value type from field
        field.metadata().set( "missing_value_type", "nan" );
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( field ) ) == 1 );
    }


    SECTION( "equals" ) {
        // missing value type from user (value set from field)
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( "equals", field ) ) == 2 );

        // missing value type from field
        field.metadata().set( "missing_value_type", "equals" );
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( field ) ) == 2 );
    }


    SECTION( "approximately-equals" ) {
        // missing value type from user (value set from field)
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( "approximately-equals", field ) ) == 3 );

        // missing value type from field
        field.metadata().set( "missing_value_type", "approximately-equals" );
        EXPECT( std::count_if( values.begin(), values.end(), MissingValue( field ) ) == 3 );
    }
}


CASE( "MissingValue from Field (DataType specialisations)" ) {
    SECTION( "real64" ) {
        std::vector<double> values( 3, 1. );
        Field field( "field", array::make_datatype<double>(), array::make_shape( values.size(), 1 ) );
        EXPECT( field.datatype().str() == array::DataType::real64().str() );
        EXPECT( !MissingValue( field ) );

        field.metadata().set( "missing_value_type", "nan" );
        EXPECT( MissingValue( field ) );
    }


    SECTION( "real32" ) {
        std::vector<float> values( 3, 1. );
        Field field( "field", array::make_datatype<float>(), array::make_shape( values.size(), 1 ) );
        EXPECT( field.datatype().str() == array::DataType::real32().str() );
        EXPECT( !MissingValue( field ) );

        field.metadata().set( "missing_value_type", "nan" );
        EXPECT( MissingValue( field ) );
    }


    SECTION( "int32" ) {
        std::vector<int> values( 3, 1 );
        Field field( "field", array::make_datatype<int>(), array::make_shape( values.size(), 1 ) );
        EXPECT( field.datatype().str() == array::DataType::int32().str() );
        EXPECT( !MissingValue( field ) );

        field.metadata().set( "missing_value_type", "equals" );
        field.metadata().set( "missing_value", static_cast<int>( missingValue ) );
        EXPECT( MissingValue( field ) );
    }


    SECTION( "int64" ) {
        std::vector<long> values( 3, 1 );
        Field field( "field", array::make_datatype<long>(), array::make_shape( values.size(), 1 ) );
        EXPECT( field.datatype().str() == array::DataType::int64().str() );
        EXPECT( !MissingValue( field ) );

        field.metadata().set( "missing_value_type", "equals" );
        field.metadata().set( "missing_value", static_cast<long>( missingValue ) );
        EXPECT( MissingValue( field ) );
    }


    SECTION( "uint64" ) {
        std::vector<unsigned long> values( 3, 1 );
        Field field( "field", array::make_datatype<unsigned long>(), array::make_shape( values.size(), 1 ) );
        EXPECT( field.datatype().str() == array::DataType::uint64().str() );
        EXPECT( !MissingValue( field ) );

        field.metadata().set( "missing_value_type", "equals" );
        field.metadata().set( "missing_value", static_cast<unsigned long>( missingValue ) );
        EXPECT( MissingValue( field ) );
    }
}


CASE( "Interpolation with MissingValue" ) {
    /*
       Set input field full of 1's, with 9 nodes
         1 ... 1 ... 1
         :     :     :
         1-----m ... 1  m: missing value
         |i   i|     :  i: interpolation on two points, this quadrilateral only
         1-----1 ... 1
     */
    RectangularDomain domain( {0, 2}, {0, 2}, "degrees" );
    Grid gridA( "L90", domain );

    const idx_t nbNodes = 9;
    ATLAS_ASSERT( gridA.size() == nbNodes );

    Mesh meshA = MeshGenerator( "structured" ).generate( gridA );

    functionspace::NodeColumns fsA( meshA );
    Field fieldA = fsA.createField<double>( option::name( "A" ) );

    fieldA.metadata().set( "missing_value", missingValue );
    fieldA.metadata().set( "missing_value_epsilon", missingValueEps );

    auto viewA = array::make_view<double, 1>( fieldA );
    for ( idx_t j = 0; j < fsA.nodes().size(); ++j ) {
        viewA( j ) = 1;
    }


    // Set output field (2 points)
    functionspace::PointCloud fsB( {PointLonLat{0.1, 0.1}, PointLonLat{0.9, 0.9}} );
    Field fieldB( "B", array::make_datatype<double>(), array::make_shape( fsB.size() ) );

    auto viewB = array::make_view<double, 1>( fieldB );
    ATLAS_ASSERT( viewB.size() == 2 );


    SECTION( "missing-if-all-missing" ) {
        Interpolation interpolation( Config( "type", "finite-element" ).set( "non_linear", "missing-if-all-missing" ),
                                     fsA, fsB );

        for ( std::string type : {"equals", "approximately-equals", "nan"} ) {
            fieldA.metadata().set( "missing_value_type", type );
            viewA( 4 ) = type == "nan" ? nan : missingValue;

            EXPECT( MissingValue( fieldA ) );
            interpolation.execute( fieldA, fieldB );

            MissingValue mv( fieldB );
            EXPECT( mv );
            EXPECT( mv( viewB( 0 ) ) == false );
            EXPECT( mv( viewB( 1 ) ) == false );
        }
    }


    SECTION( "missing-if-any-missing" ) {
        Interpolation interpolation( Config( "type", "finite-element" ).set( "non_linear", "missing-if-any-missing" ),
                                     fsA, fsB );

        for ( std::string type : {"equals", "approximately-equals", "nan"} ) {
            fieldA.metadata().set( "missing_value_type", type );
            viewA( 4 ) = type == "nan" ? nan : missingValue;

            EXPECT( MissingValue( fieldA ) );
            interpolation.execute( fieldA, fieldB );

            MissingValue mv( fieldB );
            EXPECT( mv );
            EXPECT( mv( viewB( 0 ) ) );
            EXPECT( mv( viewB( 1 ) ) );
        }
    }


    SECTION( "missing-if-heaviest-missing" ) {
        Interpolation interpolation(
            Config( "type", "finite-element" ).set( "non_linear", "missing-if-heaviest-missing" ), fsA, fsB );

        for ( std::string type : {"equals", "approximately-equals", "nan"} ) {
            fieldA.metadata().set( "missing_value_type", type );
            viewA( 4 ) = type == "nan" ? nan : missingValue;

            EXPECT( MissingValue( fieldA ) );
            interpolation.execute( fieldA, fieldB );

            MissingValue mv( fieldB );
            EXPECT( mv );
            EXPECT( mv( viewB( 0 ) ) == false );
            EXPECT( mv( viewB( 1 ) ) );
        }
    }
}


}  // namespace test
}  // namespace atlas


int main( int argc, char** argv ) {
    return atlas::test::run( argc, argv );
}
