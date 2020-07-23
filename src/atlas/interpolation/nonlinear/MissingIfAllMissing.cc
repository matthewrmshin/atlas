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


#include "atlas/interpolation/nonlinear/MissingIfAllMissing.h"

#include "eckit/types/FloatCompare.h"

#include "atlas/array.h"
#include "atlas/field/Field.h"
#include "atlas/interpolation/nonlinear/NonLinearFactory.h"
#include "atlas/runtime/Exception.h"


namespace atlas {
namespace interpolation {
namespace nonlinear {


MissingIfAllMissing::MissingIfAllMissing( const Config& config ) : NonLinear( config ) {}


bool MissingIfAllMissing::execute( NonLinear::Matrix& W, const Field& field ) const {
    // NOTE only for scalars (for now)
    auto values        = array::make_view<double, 1>( field );
    auto& missingValue = missingValue_.ref();

    // correct matrix weigths for the missing values
    // (force a missing value only if all row values are missing)
    ATLAS_ASSERT( idx_t( W.cols() ) == values.size() );

    auto data  = const_cast<Scalar*>( W.data() );
    bool modif = false;

    Size i = 0;
    Matrix::iterator it( W );
    for ( Size r = 0; r < W.rows(); ++r ) {
        const Matrix::iterator end = W.end( r );

        // count missing values, accumulate weights (disregarding missing values)
        size_t i_missing = i;
        size_t N_missing = 0;
        size_t N_entries = 0;
        double sum       = 0.;

        Matrix::iterator kt( it );
        Size k = i;
        for ( ; it != end; ++it, ++i, ++N_entries ) {
            const bool miss = missingValue( values[it.col()] );

            if ( miss ) {
                ++N_missing;
                i_missing = i;
            }
            else {
                sum += *it;
            }
        }

        // weights redistribution: zero-weight all missing values, linear re-weighting for the others;
        // the result is missing value if all values in row are missing
        if ( N_missing > 0 ) {
            if ( N_missing == N_entries || eckit::types::is_approximately_equal( sum, 0. ) ) {
                for ( Size j = k; j < k + N_entries; ++j ) {
                    data[j] = j == i_missing ? 1. : 0.;
                }
            }
            else {
                const double factor = 1. / sum;
                for ( Size j = k; j < k + N_entries; ++j, ++kt ) {
                    const bool miss = missingValue( values[kt.col()] );
                    data[j]         = miss ? 0. : ( factor * data[j] );
                }
            }
            modif = true;
        }
    }

    return modif;
}


static NonLinearFactoryBuilder<MissingIfAllMissing> __nonlinear( "missing-if-all-missing" );


}  // namespace nonlinear
}  // namespace interpolation
}  // namespace atlas
