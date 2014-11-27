/*
 * (C) Copyright 1996-2014 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */



#include <algorithm>

#include "atlas/meshgen/RGG.h"

namespace atlas {
namespace meshgen {

T95::T95()
{
  int N=48;
  int lon[] = {
    20,
    25,
    36,
    40,
    45,
    50,
    60,
    60,
    72,
    75,
    80,
    90,
    96,
    100,
    108,
    120,
    120,
    120,
    125,
    135,
    144,
    144,
    150,
    150,
    160,
    160,
    160,
    180,
    180,
    180,
    180,
    180,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192,
    192
  };
double colat[] = {
    0.02492036059421555427,
    0.05720262597323678977,
    0.08967553546914315554,
    0.12219151945674987247,
    0.15472384244808873310,
    0.18726407174005726963,
    0.21980871933238274596,
    0.25235608399078757191,
    0.28490523779441129237,
    0.31745563181617048043,
    0.35000692063955030076,
    0.38255887607470256961,
    0.41511134132612115266,
    0.44766420509684223816,
    0.48021738619824949623,
    0.51277082400921480954,
    0.54532447234592495988,
    0.57787829540015078766,
    0.61043226497516234197,
    0.64298635856011987499,
    0.67554055796049028437,
    0.70809484830577151815,
    0.74064921731856214748,
    0.77320365476802566107,
    0.80575815205564238486,
    0.83831270189731077469,
    0.87086729807659968294,
    0.90342193525120484399,
    0.93597660879965882685,
    0.96853131469881359461,
    1.00108604942508527813,
    1.03364080987421291802,
    1.06619559329555757543,
    1.09875039723791489976,
    1.13130521950450657620,
    1.16386005811532911025,
    1.19641491127544452588,
    1.22896977734808365845,
    1.26152465483166875693,
    1.29407954234003508276,
    1.32663443858526908237,
    1.35918934236269328686,
    1.39174425253759537213,
    1.42429916803338851850,
    1.45685408782091840862,
    1.48940901090868638157,
    1.52196393633378246335,
    1.55451886315335485733
  };
  setup_colat_hemisphere(N,colat,lon,RAD);
}

} // namespace meshgen
} // namespace atlas
