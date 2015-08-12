/*
 * (C) Copyright 1996-2014 ECMWF.
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 * In applying this licence, ECMWF does not waive the privileges and immunities
 * granted to it by virtue of its status as an intergovernmental organisation nor
 * does it submit to any jurisdiction.
 */



#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <limits>

#include <eckit/filesystem/PathName.h>
#include "eckit/exception/Exceptions.h"
#include "eckit/log/Log.h"
#include <eckit/config/Resource.h>
#include <eckit/runtime/Context.h>
#include "atlas/io/Gmsh.h"
#include "atlas/mpl/GatherScatter.h"
#include "atlas/util/Array.h"
#include "atlas/util/ArrayView.h"
#include "atlas/util/IndexView.h"
#include "atlas/Mesh.h"
#include "atlas/FunctionSpace.h"
#include "atlas/Field.h"
#include "atlas/FieldSet.h"
#include "atlas/Parameters.h"

using namespace eckit;

namespace atlas {
namespace io {

namespace {

static double deg = Constants::radianToDegrees();
static double rad = Constants::degreesToRadians();

class GmshFile : public std::ofstream {
public:
  GmshFile(const PathName& file_path, std::ios_base::openmode mode, int part = eckit::mpi::rank())
  {
    PathName par_path(file_path);
    if (eckit::mpi::size() == 1 || part == -1) {
      std::ofstream::open(par_path.localPath(), mode);
    } else {
      Translator<int, std::string> to_str;
      if (eckit::mpi::rank() == 0) {
        PathName par_path(file_path);
        std::ofstream par_file(par_path.localPath(), std::ios_base::out);
        for(size_t p = 0; p < eckit::mpi::size(); ++p) {
          PathName loc_path(file_path);
          loc_path = loc_path.baseName(false) + "_p" + to_str(p) + ".msh";
          par_file << "Merge \"" << loc_path << "\";" << std::endl;
        }
        par_file.close();
      }
      PathName path(file_path);
      path = path.dirName() + "/" + path.baseName(false) + "_p" + to_str(part) + ".msh";
      std::ofstream::open(path.localPath(), mode);
    }
  }
};

enum GmshElementTypes { LINE=1, TRIAG=2, QUAD=3, POINT=15 };

void write_header_ascii(std::ostream& out)
{
  out << "$MeshFormat\n";
  out << "2.2 0 "<<sizeof(double)<<"\n";
  out << "$EndMeshFormat\n";
}
void write_header_binary(std::ostream& out)
{
  out << "$MeshFormat\n";
  out << "2.2 1 "<<sizeof(double)<<"\n";
  int one = 1;
  out.write(reinterpret_cast<const char*>(&one),sizeof(int));
  out << "\n$EndMeshFormat\n";
}

template< typename DATA_TYPE >
void write_field_nodes(const Gmsh& gmsh, const FunctionSpace& function_space, Field& field, std::ostream& out)
{
  Log::info() << "writing field " << field.name() << "..." << std::endl;

  bool gather( gmsh.options.get<bool>("gather") );
  bool binary( !gmsh.options.get<bool>("ascii") );
  int nlev  = field.metadata().has("nb_levels") ? field.metadata().get<size_t>("nb_levels") : 1;
  int ndata = field.shape(0);
  int nvars = field.shape(1)/nlev;
  ArrayView<gidx_t,1    > gidx ( function_space.field( "glb_idx" ) );
  ArrayView<DATA_TYPE> data ( field );
  Array<DATA_TYPE> field_glb_arr;
  Array<gidx_t>    gidx_glb_arr;
  if( gather )
  {
    mpl::GatherScatter& fullgather = function_space.fullgather();
    ndata = fullgather.glb_dof();
    field_glb_arr.resize(ndata,field.shape(1));
    gidx_glb_arr.resize(ndata);
    ArrayView<DATA_TYPE> data_glb( field_glb_arr );
    ArrayView<gidx_t,1> gidx_glb( gidx_glb_arr );
    fullgather.gather( gidx, gidx_glb );
    fullgather.gather( data, data_glb );
    gidx = ArrayView<gidx_t,1>( gidx_glb_arr );
    data = data_glb;
  }

  std::vector<long> lev;
  std::vector<long> gmsh_levels;
  gmsh.options.get("levels",gmsh_levels);
  if( gmsh_levels.empty() || nlev == 1 )
  {
    lev.resize(nlev);
    for (int ilev=0; ilev<nlev; ++ilev)
      lev[ilev] = ilev;
  }
  else
  {
    lev = gmsh_levels;
  }
  for (int ilev=0; ilev<lev.size(); ++ilev)
  {
    int jlev = lev[ilev];
    if( ( gather && eckit::mpi::rank() == 0 ) || !gather )
    {
      char field_lev[6];
      if( field.metadata().has("nb_levels") )
        std::sprintf(field_lev, "[%03d]",jlev);
      double time = field.metadata().has("time") ? field.metadata().get<double>("time") : 0.;
      int step = field.metadata().has("step") ? field.metadata().get<size_t>("step") : 0 ;
      out << "$NodeData\n";
      out << "1\n";
      out << "\"" << field.name() << field_lev << "\"\n";
      out << "1\n";
      out << time << "\n";
      out << "4\n";
      out << step << "\n";
      if     ( nvars == 1 ) out << nvars << "\n";
      else if( nvars <= 3 ) out << 3     << "\n";
      out << ndata << "\n";
      out << eckit::mpi::rank() << "\n";

      if( binary )
      {
        if( nvars == 1)
        {
          double value;
          for( int n = 0; n < ndata; ++n )
          {
            out.write(reinterpret_cast<const char*>(&gidx(n)),sizeof(int));
            value = data(n,jlev*nvars+0);
            out.write(reinterpret_cast<const char*>(&value),sizeof(double));
          }
        }
        else if( nvars <= 3 )
        {
          double value[3] = {0,0,0};
          for( size_t n = 0; n < ndata; ++n )
          {
            out.write(reinterpret_cast<const char*>(&gidx(n)),sizeof(int));
            for( int v=0; v<nvars; ++v)
              value[v] = data(n,jlev*nvars+v);
            out.write(reinterpret_cast<const char*>(&value),sizeof(double)*3);
          }
        }
        out << "\n";
      }
      else
      {
        if( nvars == 1)
        {
          for( int n = 0; n < ndata; ++n )
          {
            ASSERT( jlev*nvars < data.shape(1) );
            ASSERT( n < gidx.shape(0) );
            out << gidx(n) << " " << data(n,jlev*nvars+0) << "\n";
          }
        }
        else if( nvars <= 3 )
        {
          std::vector<DATA_TYPE> data_vec(3,0.);
          for( size_t n = 0; n < ndata; ++n )
          {
            out << gidx(n);
            for( int v=0; v<nvars; ++v)
              data_vec[v] = data(n,jlev*nvars+v);
            for( int v=0; v<3; ++v)
              out << " " << data_vec[v];
            out << "\n";
          }
        }

      }
      out << "$EndNodeData\n";
    }
  }
}

template< typename DATA_TYPE >
void write_field_elems(const Gmsh& gmsh, const FunctionSpace& function_space, Field& field, std::ostream& out)
{
  Log::info() << "writing field " << field.name() << "..." << std::endl;
  bool gather( gmsh.options.get<bool>("gather") );
  bool binary( !gmsh.options.get<bool>("ascii") );
  int nlev = field.metadata().has("nb_levels") ? field.metadata().get<size_t>("nb_levels") : 1;
  int ndata = field.shape(0);
  int nvars = field.shape(1)/nlev;
  ArrayView<gidx_t,1    > gidx ( function_space.field( "glb_idx" ) );
  ArrayView<DATA_TYPE> data ( field );
  Array<DATA_TYPE> field_glb_arr;
  Array<gidx_t   > gidx_glb_arr;
  if( gather )
  {
    mpl::GatherScatter& fullgather = function_space.fullgather();
    ndata = fullgather.glb_dof();
    field_glb_arr.resize(ndata,field.shape(1));
    gidx_glb_arr.resize(ndata);
    ArrayView<DATA_TYPE> data_glb( field_glb_arr );
    ArrayView<gidx_t,1> gidx_glb( gidx_glb_arr );
    fullgather.gather( gidx, gidx_glb );
    fullgather.gather( data, data_glb );
    gidx = ArrayView<gidx_t,1>( gidx_glb_arr );
    data = data_glb;
  }

  double time = field.metadata().has("time") ? field.metadata().get<double>("time") : 0.;
  size_t step = field.metadata().has("step") ? field.metadata().get<size_t>("step") : 0 ;

  int nnodes = IndexView<int,2>( function_space.field("nodes") ).shape(1);

  for (int jlev=0; jlev<nlev; ++jlev)
  {
    char field_lev[6];
    if( field.metadata().has("nb_levels") )
      std::sprintf(field_lev, "[%03d]",jlev);

    out << "$ElementNodeData\n";
    out << "1\n";
    out << "\"" << field.name() << field_lev << "\"\n";
    out << "1\n";
    out << time << "\n";
    out << "4\n";
    out << step << "\n";
    if     ( nvars == 1 ) out << nvars << "\n";
    else if( nvars <= 3 ) out << 3     << "\n";
    out << ndata << "\n";
    out << eckit::mpi::rank() << "\n";

    if( binary )
    {
      if( nvars == 1)
      {
        double value;
        for (size_t jelem=0; jelem<ndata; ++jelem)
        {
          out.write(reinterpret_cast<const char*>(&gidx(jelem)),sizeof(int));
          out.write(reinterpret_cast<const char*>(&nnodes),sizeof(int));
          for (size_t n=0; n<nnodes; ++n)
          {
            value = data(jelem,jlev);
            out.write(reinterpret_cast<const char*>(&value),sizeof(double));
          }
        }
      }
      else if( nvars <= 3 )
      {
        double value[3] = {0,0,0};
        for (size_t jelem=0; jelem<ndata; ++jelem)
        {
          out << gidx(jelem) << " " << nnodes;
          for (size_t n=0; n<nnodes; ++n)
          {
            for( int v=0; v<nvars; ++v)
              value[v] = data(jelem,jlev*nvars+v);
            out.write(reinterpret_cast<const char*>(&value),sizeof(double)*3);
          }
        }
      }
      out <<"\n";
    }
    else
    {
      if( nvars == 1)
      {
        for (size_t jelem=0; jelem<ndata; ++jelem)
        {
          out << gidx(jelem) << " " << nnodes;
          for (size_t n=0; n<nnodes; ++n)
            out << " " << data(jelem,jlev);
          out <<"\n";
        }
      }
      else if( nvars <= 3 )
      {
        std::vector<DATA_TYPE> data_vec(3,0.);
        for (size_t jelem=0; jelem<ndata; ++jelem)
        {
          out << gidx(jelem) << " " << nnodes;
          for (size_t n=0; n<nnodes; ++n)
          {
            for( int v=0; v<nvars; ++v)
              data_vec[v] = data(jelem,jlev*nvars+v);
            for( int v=0; v<3; ++v)
              out << " " << data_vec[v];
          }
          out <<"\n";
        }
      }
    }
    out << "$EndElementNodeData\n";
  }
}

void swap_bytes(char *array, int size, int n)
  {
    char *x = new char[size];
    for(int i = 0; i < n; i++) {
      char *a = &array[i * size];
      memcpy(x, a, size);
      for(int c = 0; c < size; c++)
        a[size - 1 - c] = x[c];
    }
    delete [] x;
  }


} // end anonymous namespace

Gmsh::Gmsh()
{
  // which field holds the Nodes
  options.set<std::string>("nodes", Resource<std::string>("atlas.gmsh.nodes", "lonlat"));

  // Gather fields to one proc before writing
  options.set<bool>("gather",  Resource<bool>("atlas.gmsh.gather",  false));

  // Output of ghost nodes / elements
  options.set<bool>("ghost",   Resource<bool>("atlas.gmsh.ghost",   false));

  // ASCII format (true) or binary (false)
  options.set<bool>("ascii",   Resource<bool>("atlas.gmsh.ascii",   true ));

  // Output of elements
  options.set<bool>("elements",Resource<bool>("atlas.gmsh.elements",true ));

  // Output of edges
  options.set<bool>("edges",   Resource<bool>("atlas.gmsh.edges",   true ));

  // Radius of the planet
  options.set<double>("radius",   Resource<bool>("atlas.gmsh.radius", 1.0 ));

  // Levels of fields to use
  options.set< std::vector<long> >("levels", Resource< std::vector<long> >("atlas.gmsh.levels", std::vector<long>() ) );
}

Gmsh::~Gmsh()
{
}

Mesh* Gmsh::read(const PathName& file_path) const
{
  Mesh* mesh = new Mesh();
  Gmsh::read(file_path,*mesh);
  return mesh;
}

void Gmsh::read(const PathName& file_path, Mesh& mesh ) const
{
  std::ifstream file;
  file.open( file_path.localPath() , std::ios::in | std::ios::binary );
  if( !file.is_open() )
    throw CantOpenFile(file_path);

  std::string line;

  while(line != "$MeshFormat")
    std::getline(file,line);
  double version;
  int binary;
  int size_of_real;
  file >> version >> binary >> size_of_real;

  while(line != "$Nodes")
    std::getline(file,line);

  // Create nodes
    size_t nb_nodes;
  file >> nb_nodes;

  std::vector<size_t> extents(2);

  extents[0] = nb_nodes;
  extents[1] = FunctionSpace::UNDEF_VARS;

  if( mesh.has_function_space("nodes") )
  {
    if( mesh.function_space("nodes").shape(0)!= nb_nodes )
      throw Exception("existing nodes function space has incompatible number of nodes",Here());
  }
  else
  {
    mesh.create_function_space( "nodes", "Lagrange_P0", extents )
      .metadata().set<long>("type",Entity::NODES);
  }

  FunctionSpace& nodes = mesh.function_space("nodes");

  nodes.create_field<double>("xyz",3,IF_EXISTS_RETURN);
  nodes.create_field<gidx_t>("glb_idx",1,IF_EXISTS_RETURN);
  nodes.create_field<int>("partition",1,IF_EXISTS_RETURN);

  ArrayView<double,2> coords         ( nodes.field("xyz")    );
  ArrayView<gidx_t,1> glb_idx        ( nodes.field("glb_idx")        );
  ArrayView<int,   1> part           ( nodes.field("partition")      );

  std::map<int,int> glb_to_loc;
  int g;
  double x,y,z;
  double xyz[3];
  double xmax = -std::numeric_limits<double>::max();
  double zmax = -std::numeric_limits<double>::max();
  gidx_t max_glb_idx=0;
  while(binary && file.peek()=='\n') file.get();
  for( size_t n = 0; n < nb_nodes; ++n )
  {
    if( binary )
    {
      file.read(reinterpret_cast<char*>(&g), sizeof(int));
      file.read(reinterpret_cast<char*>(&xyz), sizeof(double)*3);
      x = xyz[XX];
      y = xyz[YY];
      z = xyz[ZZ];
    }
    else
    {
      file >> g >> x >> y >> z;
    }
    glb_idx(n) = g;
    coords(n,XX) = x;
    coords(n,YY) = y;
    coords(n,ZZ) = z;
    glb_to_loc[ g ] = n;
    part(n) = 0;
    max_glb_idx = std::max(max_glb_idx, static_cast<gidx_t>(g));
    xmax = std::max(x,xmax);
    zmax = std::max(z,zmax);
  }
  if( xmax < 4*M_PI && zmax == 0. )
  {
    for( size_t n = 0; n < nb_nodes; ++n )
    {
      coords(n,XX) *= deg;
      coords(n,YY) *= deg;
    }
  }
  for (int i=0; i<3; ++i)
    std::getline(file,line);

  nodes.metadata().set("nb_owned",nb_nodes);
  nodes.metadata().set("max_glb_idx",max_glb_idx);

  int nb_elements=0;

  while(line != "$Elements")
    std::getline(file,line);

  file >> nb_elements;

  if( binary )
  {
        while(file.peek()=='\n') file.get();
    int accounted_elems = 0;
    while( accounted_elems < nb_elements )
    {
      int header[3];
      int data[100];
      file.read(reinterpret_cast<char*>(&header),sizeof(int)*3);

      int etype = header[0];
      int netype = header[1];
      int ntags = header[2];
      accounted_elems += netype;
      std::string name;
      int nnodes_per_elem;
      switch( etype ) {
      case(QUAD):
        nnodes_per_elem = 4;
        name = "quads";
        break;
      case(TRIAG):
        nnodes_per_elem = 3;
        name = "triags";
        break;
      case(LINE):
        nnodes_per_elem = 3;
        name = "edges";
        break;
      default:
        std::cout << "etype " << etype << std::endl;
        throw Exception("ERROR: element type not supported",Here());
      }

      extents[0] = netype;

      FunctionSpace& fs = mesh.create_function_space( name, "Lagrange_P1", extents );

      fs.metadata().set<long>("type",static_cast<int>(Entity::ELEMS));

      IndexView<int,2> conn  ( fs.create_field<int>("nodes",         4) );
      ArrayView<gidx_t,1> egidx ( fs.create_field<int>("glb_idx",       1) );
      ArrayView<int,1> epart ( fs.create_field<int>("partition",     1) );

      int dsize = 1+ntags+nnodes_per_elem;
      int part;
      for (int e=0; e<netype; ++e)
      {
        file.read(reinterpret_cast<char*>(&data),sizeof(int)*dsize);
        part = 0;
        egidx(e) = data[0];
        epart(e) = part;
        for(int n=0; n<nnodes_per_elem; ++n)
          conn(e,n) = glb_to_loc[data[1+ntags+n]];
      }
    }
  }
  else
  {
    // Find out which element types are inside
    int position = file.tellg();
    std::vector<int> nb_etype(20,0);
    int elements_max_glb_idx(0);
    int etype;
    for (int e=0; e<nb_elements; ++e)
    {
      file >> g >> etype; std::getline(file,line); // finish line
      ++nb_etype[etype];
      elements_max_glb_idx = std::max(elements_max_glb_idx,g);
    }

    // Allocate data structures for quads, triags, edges

    int nb_quads = nb_etype[QUAD];
    extents[0] = nb_quads;
    FunctionSpace& quads      = mesh.create_function_space( "quads", "Lagrange_P1", extents );
    quads.metadata().set<long>("type",static_cast<int>(Entity::ELEMS));
    IndexView<int,2> quad_nodes          ( quads.create_field<int>("nodes",         4) );
    ArrayView<gidx_t,1> quad_glb_idx        ( quads.create_field<gidx_t>("glb_idx",       1) );
    ArrayView<int,1> quad_part           ( quads.create_field<int>("partition",     1) );

    int nb_triags = nb_etype[TRIAG];
    extents[0] = nb_triags;
    FunctionSpace& triags      = mesh.create_function_space( "triags", "Lagrange_P1", extents );
    triags.metadata().set<long>("type",static_cast<int>(Entity::ELEMS));
    IndexView<int,2> triag_nodes          ( triags.create_field<int>("nodes",         3) );
    ArrayView<gidx_t,1> triag_glb_idx        ( triags.create_field<gidx_t>("glb_idx",       1) );
    ArrayView<int,1> triag_part           ( triags.create_field<int>("partition",     1) );

    int nb_edges = nb_etype[LINE];
    IndexView<int,2> edge_nodes;
    ArrayView<gidx_t,1> edge_glb_idx;
    ArrayView<int,1> edge_part;

    if( nb_edges > 0 )
    {
      extents[0] = nb_edges;
      FunctionSpace& edges      = mesh.create_function_space( "edges", "Lagrange_P1", extents );
      edges.metadata().set<long>("type",static_cast<int>(Entity::FACES));
      edge_nodes   = IndexView<int,2> ( edges.create_field<int>("nodes",         2) );
      edge_glb_idx = ArrayView<gidx_t,1> ( edges.create_field<gidx_t>("glb_idx",       1) );
      edge_part    = ArrayView<int,1> ( edges.create_field<int>("partition",     1) );
    }

    // Now read all elements
    file.seekg(position,std::ios::beg);
    int gn0, gn1, gn2, gn3;
    int quad=0, triag=0, edge=0;
    int ntags, tags[100];
    for (int e=0; e<nb_elements; ++e)
    {
      file >> g >> etype >> ntags;
      for( int t=0; t<ntags; ++t ) file >> tags[t];
      int part=0;
      if( ntags > 3 ) part = std::max( part, *std::max_element(tags+3,tags+ntags-1) ); // one positive, others negative
      switch( etype )
      {
        case(QUAD):
          file >> gn0 >> gn1 >> gn2 >> gn3;
          quad_glb_idx(quad) = g;
          quad_part(quad) = part;
          quad_nodes(quad,0) = glb_to_loc[gn0];
          quad_nodes(quad,1) = glb_to_loc[gn1];
          quad_nodes(quad,2) = glb_to_loc[gn2];
          quad_nodes(quad,3) = glb_to_loc[gn3];
          ++quad;
          break;
        case(TRIAG):
          file >> gn0 >> gn1 >> gn2;
          triag_glb_idx(triag) = g;
          triag_part(triag) = part;
          triag_nodes(triag,0) = glb_to_loc[gn0];
          triag_nodes(triag,1) = glb_to_loc[gn1];
          triag_nodes(triag,2) = glb_to_loc[gn2];
          ++triag;
          break;
        case(LINE):
          file >> gn0 >> gn1;
          edge_glb_idx(edge) = g;
          edge_part(edge) = part;
          edge_nodes(edge,0) = glb_to_loc[gn0];
          edge_nodes(edge,1) = glb_to_loc[gn1];
          ++edge;
          break;
        case(POINT):
          file >> gn0;
          break;
        default:
          std::cout << "etype " << etype << std::endl;
          throw Exception("ERROR: element type not supported",Here());
      }
    }
    quads.metadata().set("nb_owned",nb_etype[QUAD]);
    triags.metadata().set("nb_owned",nb_etype[TRIAG]);
    quads.metadata().set("max_glb_idx",elements_max_glb_idx);
    triags.metadata().set("max_glb_idx",elements_max_glb_idx);

    if( nb_edges > 0 )
    {
      mesh.function_space("edges").metadata().set("nb_owned",nb_etype[LINE]);
      mesh.function_space("edges").metadata().set("max_glb_idx",elements_max_glb_idx);
    }

  }

  file.close();
}


void Gmsh::write(const Mesh& mesh, const PathName& file_path) const
{
  int part = mesh.metadata().has("part") ? mesh.metadata().get<size_t>("part") : eckit::mpi::rank();
  bool include_ghost_elements = options.get<bool>("ghost");

  std::string nodes_field = options.get<std::string>("nodes");

  FunctionSpace& nodes    = mesh.function_space( "nodes" );
  ArrayView<double,2> coords  ( nodes.field( nodes_field ) );
  ArrayView<gidx_t,   1> glb_idx ( nodes.field( "glb_idx" ) );

  const size_t surfdim = coords.shape(1); // nb of variables in coords

  ASSERT(surfdim == 2 || surfdim == 3);

  size_t nb_nodes = nodes.shape(0);

  // Find out number of elements to write
  int nb_quads(0);
  if( mesh.has_function_space("quads") )
  {
    FunctionSpace& quads       = mesh.function_space( "quads" );
    nb_quads = quads.metadata().has("nb_owned") ? quads.metadata().get<size_t>("nb_owned") : quads.shape(0);
    if( include_ghost_elements == true )
      nb_quads = quads.shape(0);
    if( options.get<bool>("elements") == false )
      nb_quads = 0;
  }

  int nb_triags(0);
  if( mesh.has_function_space("triags") )
  {
    FunctionSpace& triags       = mesh.function_space( "triags" );
    nb_triags = triags.metadata().has("nb_owned") ? triags.metadata().get<size_t>("nb_owned") : triags.shape(0);
    if( include_ghost_elements == true )
      nb_triags = triags.shape(0);
    if( options.get<bool>("elements") == false )
      nb_triags = 0;
  }

  int nb_edges(0);
  if( mesh.has_function_space("edges") )
  {
    FunctionSpace& edges       = mesh.function_space( "edges" );
    nb_edges = edges.metadata().has("nb_owned") ? edges.metadata().get<size_t>("nb_owned") :  edges.shape(0);
    if( include_ghost_elements == true )
      nb_edges = edges.shape(0);
    if( options.get<bool>("edges") == false && (nb_triags+nb_quads) > 0 )
      nb_edges = 0;
  }

  Log::info() << "writing mesh to gmsh file " << file_path << std::endl;

  bool binary = !options.get<bool>("ascii");

  openmode mode = std::ios::out;
  if( binary )
    mode = std::ios::out | std::ios::binary;
  GmshFile file(file_path,mode,part);

  // Header
  if( binary )
    write_header_binary(file);
  else
    write_header_ascii(file);

  // Nodes
  file << "$Nodes\n";
  file << nb_nodes << "\n";
  double xyz[3] = {0.,0.,0.};
  for( size_t n = 0; n < nb_nodes; ++n )
  {
    int g = glb_idx(n);

    for(size_t d = 0; d < surfdim; ++d)
        xyz[d] = coords(n,d);

    if( binary )
    {
        file.write(reinterpret_cast<const char*>(&g), sizeof(int));
        file.write(reinterpret_cast<const char*>(&xyz), sizeof(double)*3 );
    }
    else
    {
        file << g << " " << xyz[XX] << " " << xyz[YY] << " " << xyz[ZZ] << "\n";
    }
  }
  if( binary ) file << "\n";
  file << "$EndNodes\n";

  // Elements
  file << "$Elements\n";

  if( binary)
  {
    file << nb_quads+nb_triags+nb_edges << "\n";
    int header[3];
    int data[9];
    if( nb_quads )
    {
      FunctionSpace& quads       = mesh.function_space( "quads" );
      IndexView<int,2> quad_nodes   ( quads.field( "nodes" ) );
      ArrayView<gidx_t,1> quad_glb_idx ( quads.field( "glb_idx" ) );
      ArrayView<int,1> quad_part    ( quads.field( "partition" ) );
      header[0] = 3;         // elm_type = QUAD
      header[1] = nb_quads;  // nb_elems
      header[2] = 4;         // nb_tags
      file.write(reinterpret_cast<const char*>(&header), sizeof(int)*3 );
      data[1]=1;
      data[2]=1;
      data[3]=1;
      for( int e=0; e<nb_quads; ++e)
      {
        data[0] = quad_glb_idx(e);
        data[4] = quad_part(e);
        for( int n=0; n<4; ++n )
          data[5+n] = glb_idx( quad_nodes(e,n) );
        file.write(reinterpret_cast<const char*>(&data), sizeof(int)*9 );
      }
    }
    if( nb_triags )
    {
      FunctionSpace& triags      = mesh.function_space( "triags" );
      IndexView<int,2> triag_nodes   ( triags.field( "nodes" ) );
      ArrayView<gidx_t,1> triag_glb_idx ( triags.field( "glb_idx" ) );
      ArrayView<int,1> triag_part    ( triags.field( "partition" ) );
      header[0] = 2;         // elm_type = TRIAG
      header[1] = nb_triags; // nb_elems
      header[2] = 4;         // nb_tags
      file.write(reinterpret_cast<const char*>(&header), sizeof(int)*3 );
      data[1]=1;
      data[2]=1;
      data[3]=1;
      for( int e=0; e<nb_triags; ++e)
      {
        data[0] = triag_glb_idx(e);
        data[4] = triag_part(e);
        for( int n=0; n<3; ++n )
          data[5+n] = glb_idx( triag_nodes(e,n) );
        file.write(reinterpret_cast<const char*>(&data), sizeof(int)*8 );
      }
    }
    if( nb_edges )
    {
      FunctionSpace& edges       = mesh.function_space( "edges" );
      IndexView<int,2> edge_nodes   ( edges.field( "nodes" ) );
      ArrayView<gidx_t,1> edge_glb_idx ( edges.field( "glb_idx" ) );
      header[0] = 1;         // elm_type = LINE
      header[1] = nb_edges;  // nb_elems
      if( edges.has_field("partition") )
      {
        header[2] = 4;         // nb_tags
        data[1]=1;
        data[2]=1;
        data[3]=1;
        ArrayView<int,1> edge_part ( edges.field( "partition" ) );
        for( int e=0; e<nb_edges; ++e)
        {
          data[0] = edge_glb_idx(e);
          data[4] = edge_part(e);
          for( int n=0; n<2; ++n )
            data[5+n] = glb_idx(edge_nodes(e,n) );
          file.write(reinterpret_cast<const char*>(&data), sizeof(int)*7 );
        }
      }
      else
      {
        header[2] = 2;         // nb_tags
        data[1]=1;
        data[2]=1;
        for( int e=0; e<nb_edges; ++e)
        {
          data[0] = edge_glb_idx(e);
          file << edge_glb_idx(e) << " 1 2 1 1";
          for( int n=0; n<2; ++n )
            data[3+n] = glb_idx(edge_nodes(e,n) );
          file.write(reinterpret_cast<const char*>(&data), sizeof(int)*5 );
        }
      }
    }

    file << "\n";
  }
  else
  {
    file << nb_quads+nb_triags+nb_edges << "\n";
    if( nb_quads )
    {
      FunctionSpace& quads       = mesh.function_space( "quads" );
      IndexView<int,2> quad_nodes   ( quads.field( "nodes" ) );
      ArrayView<gidx_t,1> quad_glb_idx ( quads.field( "glb_idx" ) );
      ArrayView<int,1> quad_part    ( quads.field( "partition" ) );

      for( int e=0; e<nb_quads; ++e)
      {
        file << quad_glb_idx(e) << " 3 4 1 1 1 " << quad_part(e);
        for( int n=0; n<4; ++n )
          file << " " << glb_idx( quad_nodes(e,n) );
        file << "\n";
      }
    }
    if( nb_triags )
    {
      FunctionSpace& triags      = mesh.function_space( "triags" );
      IndexView<int,2> triag_nodes   ( triags.field( "nodes" ) );
      ArrayView<gidx_t,1> triag_glb_idx ( triags.field( "glb_idx" ) );
      ArrayView<int,1> triag_part    ( triags.field( "partition" ) );

      for( int e=0; e<nb_triags; ++e)
      {
        file << triag_glb_idx(e) << " 2 4 1 1 1 " << triag_part(e);
        for( int n=0; n<3; ++n )
          file << " " << glb_idx( triag_nodes(e,n) );
        file << "\n";
      }
    }
    if( nb_edges )
    {
      FunctionSpace& edges       = mesh.function_space( "edges" );
      IndexView<int,2> edge_nodes   ( edges.field( "nodes" ) );
      ArrayView<gidx_t,1> edge_glb_idx ( edges.field( "glb_idx" ) );
      if( edges.has_field("partition") )
      {
        ArrayView<int,1> edge_part ( edges.field( "partition" ) );
        for( int e=0; e<nb_edges; ++e)
        {
          file << edge_glb_idx(e) << " 1 4 1 1 1 " << edge_part(e);
          for( int n=0; n<2; ++n )
            file << " " << glb_idx( edge_nodes(e,n) );
          file << "\n";
        }
      }
      else
      {
        for( int e=0; e<nb_edges; ++e)
        {
          file << edge_glb_idx(e) << " 1 2 1 1";
          for( int n=0; n<2; ++n )
            file << " " << glb_idx( edge_nodes(e,n) );
          file << "\n";
        }
      }
    }
  }
  file << "$EndElements\n";
  file << std::flush;
  file.close();



  // Optional mesh information file
  if( options.has("info") && options.get<bool>("info") )
  {
    PathName mesh_info(file_path);
    mesh_info = mesh_info.dirName()+"/"+mesh_info.baseName(false)+"_info.msh";

    if (nodes.has_field("partition"))
    {
      write(nodes.field("partition"),mesh_info,std::ios_base::out);
    }

    if (nodes.has_field("dual_volumes"))
    {
      write(nodes.field("dual_volumes"),mesh_info,std::ios_base::app);
    }

    if (nodes.has_field("dual_delta_sph"))
    {
      write(nodes.field("dual_delta_sph"),mesh_info,std::ios_base::app);
    }

    if( mesh.has_function_space("edges") )
    {
      FunctionSpace& edges = mesh.function_space( "edges" );

      if (edges.has_field("dual_normals"))
      {
        write(edges.field("dual_normals"),mesh_info,std::ios_base::app);
      }

      if (edges.has_field("skewness"))
      {
        write(edges.field("skewness"),mesh_info,std::ios_base::app);
      }

      if (edges.has_field("arc_length"))
      {
        write(edges.field("arc_length"),mesh_info,std::ios_base::app);
      }
    }
  }

}

void Gmsh::write(FieldSet& fieldset, const PathName& file_path, openmode mode) const
{
  bool is_new_file = (mode != std::ios_base::app || !file_path.exists() );
  bool gather = options.has("gather") ? options.get<bool>("gather") : false;
  GmshFile file(file_path,mode,gather?-1:eckit::mpi::rank());

  Log::info() << "writing fieldset " << fieldset.name() << " to gmsh file " << file_path << std::endl;

  // Header
  if( is_new_file )
    write_header_ascii(file);

  // Fields
  for(size_t field_idx = 0; field_idx < fieldset.size(); ++field_idx)
  {
    Field& field = fieldset[field_idx];
    FunctionSpace& function_space = field.function_space();

    if( !function_space.metadata().has("type") )
    {
      throw Exception("function_space "+function_space.name()+" has no type.. ?");
    }

    if( function_space.metadata().get<long>("type") == Entity::NODES )
    {
      if     ( field.datatype() == DataType::int32()  ) {  write_field_nodes<int   >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::int64()  ) {  write_field_nodes<long  >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::real32() ) {  write_field_nodes<float >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::real64() ) {  write_field_nodes<double>(*this,function_space,field,file); }
    }
    else if( function_space.metadata().get<long>("type") == Entity::ELEMS
          || function_space.metadata().get<long>("type") == Entity::FACES )
    {
      if     ( field.datatype() == DataType::int32()  ) {  write_field_elems<int   >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::int64()  ) {  write_field_elems<long  >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::real32() ) {  write_field_elems<float >(*this,function_space,field,file); }
      else if( field.datatype() == DataType::real64() ) {  write_field_elems<double>(*this,function_space,field,file); }
    }
    file << std::flush;
  }

  file.close();
}

void Gmsh::write(Field& field, const PathName& file_path, openmode mode) const
{
  bool is_new_file = (mode != std::ios_base::app || !file_path.exists() );
  bool binary( !options.get<bool>("ascii") );
  if ( binary ) mode |= std::ios_base::binary;
  bool gather = options.has("gather") ?  options.get<bool>("gather") : false;
  GmshFile file(file_path,mode,gather?-1:eckit::mpi::rank());

  Log::info() << "writing field " << field.name() << " to gmsh file " << file_path << std::endl;

  // Header
  if( is_new_file )
  {
    if( binary )
      write_header_binary(file);
    else
      write_header_ascii(file);
  }

  // Field
  FunctionSpace& function_space = field.function_space();

  if( !function_space.metadata().has("type") )
  {
    throw Exception("function_space "+function_space.name()+" has no type.. ?");
  }

  if( function_space.metadata().get<long>("type") == Entity::NODES )
  {
    if     ( field.datatype() == DataType::int32()  ) {  write_field_nodes<int   >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::int64()  ) {  write_field_nodes<long  >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::real32() ) {  write_field_nodes<float >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::real64() ) {  write_field_nodes<double>(*this,function_space,field,file); }
  }
  else if( function_space.metadata().get<long>("type") == Entity::ELEMS ||
           function_space.metadata().get<long>("type") == Entity::FACES )
  {
    if     ( field.datatype() == DataType::int32()  ) {  write_field_elems<int   >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::int64()  ) {  write_field_elems<long  >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::real32() ) {  write_field_elems<float >(*this,function_space,field,file); }
    else if( field.datatype() == DataType::real64() ) {  write_field_elems<double>(*this,function_space,field,file); }
  }
  file << std::flush;
  file.close();
}

// ------------------------------------------------------------------
// C wrapper interfaces to C++ routines

Gmsh* atlas__Gmsh__new () {
  return new Gmsh();
}

void atlas__Gmsh__delete (Gmsh* This) {
  delete This;
}

Mesh* atlas__Gmsh__read (Gmsh* This, char* file_path) {
  return This->read( PathName(file_path) );
}

void atlas__Gmsh__write (Gmsh* This, Mesh* mesh, char* file_path) {
  This->write( *mesh, PathName(file_path) );
}

Mesh* atlas__read_gmsh (char* file_path)
{
  return Gmsh().read(PathName(file_path));
}

void atlas__write_gmsh_mesh (Mesh* mesh, char* file_path) {
  Gmsh writer;
  writer.write( *mesh, PathName(file_path) );
}

void atlas__write_gmsh_fieldset (FieldSet* fieldset, char* file_path, int mode) {
  Gmsh writer;
  writer.write( *fieldset, PathName(file_path) );
}

void atlas__write_gmsh_field (Field* field, char* file_path, int mode) {
  Gmsh writer;
  writer.write( *field, PathName(file_path) );
}

// ------------------------------------------------------------------

} // namespace io
} // namespace atlas

