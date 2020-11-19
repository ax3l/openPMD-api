/* Copyright 2020 Junmin Gu, Axel Huebl
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include <openPMD/openPMD.hpp>
#include <openPMD/auxiliary/Environment.hpp>
#include <openPMD/benchmark/Timer.hpp>

#include <mpi.h>

#include <iostream>
#include <memory>
#include <vector>


using std::cout;
using namespace openPMD;

static std::chrono::time_point<std::chrono::system_clock> program_start = std::chrono::system_clock::now();

/** createData
 *
 * generate a shared ptr of given size  with given type & default value
 *
 * @tparam T            data type
 * @param size          data size
 * @param val           data value by default
 * @param increment     increase value for each index
 * @return a shared pointer with new data
 */
template<typename T>
std::shared_ptr< T >
createData( const unsigned long& size, const T& val, bool increment = false )
{
    auto E = std::shared_ptr< T > {
    new T[size], []( T * d ){ delete[] d; }
    };

    for( unsigned long i = 0ul; i < size; i++ )
    {
        if(increment)
            E.get()[i] = val+i;
        else
            E.get()[i] = val;
    }
    return E;
}

/** Find supported backends
 *
 * This looks for ADIOS2 or H5.
 */
std::vector< std::string >
getBackends()
{
    std::vector<std::string> res;
#if openPMD_HAVE_ADIOS2
    if( auxiliary::getEnvString( "OPENPMD_BP_BACKEND", "NOT_SET" ) != "ADIOS1" )
        res.emplace_back(".bp");
#endif

#if openPMD_HAVE_HDF5
    res.emplace_back(".h5");
#endif
    return res;
}

/** Parameters for the Test
 *
 * Input data and partitioning.
 */
class TestInput
{
public:
    TestInput() =  default;

    /** Get Segments (partitions)
    *
    * return number of partitions along the long dimension
    * m_Seg can be set from input
    * exception is when h5 collective mode is on. m_Seg=1
    */
    unsigned int GetSeg() const
    {
        if( m_Backend == ".h5" )
            if( auxiliary::getEnvString( "OPENPMD_HDF5_INDEPENDENT", "ON" ) != "ON" )
                return 1;
        return m_Seg;
    }

    /** Get offset and count in data for rank
    *
    * create unbalanced load if (step % 3 == 1) %% (m_MPISize >= 2)
    * move loads on rank that  (% 10 = 0) to the next rank
    *
    * @param offset      return
    * @param count       return
    * @param step        iteration step
    */
    void GetRankCountOffset(unsigned long& offset, unsigned long& count, int& step) const
    {
        count = m_Bulk;
        offset = m_Bulk * m_MPIRank;

        if (!m_Unbalance)
          return;

        if (m_MPISize < 2)
          return;

        if (step % 3 != 1)
          return;

        if (m_MPIRank % 10 == 0)
            count = 0;

        if (m_MPIRank % 10 == 1)
        {
            offset -= m_Bulk;
            count += m_Bulk;
        }
    }

    /**
    * Decide the distribution blocks for this rank
    *
    * @ param step       iteration step
    */
    void setBlockDistributionInRank(int step)
    {
        unsigned long rankOffset, rankCount;
        GetRankCountOffset(rankOffset, rankCount, step);

        if (0 == rankCount)
          return;

        // many small writes
        srand(time(NULL) * (m_MPIRank  + m_MPISize) );

        auto nBlocks = GetSeg();
        if ((rankCount / nBlocks) <= 1)
          nBlocks = 1;

        m_InRankDistribution.clear();
        unsigned long counter = 0ul;
        for( unsigned long i = 0ul; i < nBlocks; i++ )
        {
           unsigned long blockSize = rankCount/nBlocks;
           if ((rankCount % nBlocks != 0) && (i == (nBlocks -1)))
               blockSize = rankCount - blockSize * (nBlocks -1);

           m_InRankDistribution.push_back(std::make_pair(rankOffset + counter, blockSize));
           counter += blockSize;
        }
    }


    /**
    * Run all the tests: (1D/2D) * (Group/File based) * (Un/balanced)
    *
    * @param nDim      mesh dimention, currently 1D or 2D
    *
    */
    void run(int nDim)
    {
        std::string balance = "b";
        if( m_Unbalance )
            balance = "u";

        { // file based
            std::ostringstream s;
            s << "../samples/8a_parallel_"<<nDim<<"D"<<balance<<"_%07T"<<m_Backend;

            std::string filename = s.str();

            {
                std::string tag = "Writing: "+filename ;
                benchmark::Timer kk( tag.c_str(), m_MPIRank, program_start );

                for( int step = 1; step <= m_Steps; step++ )
                {
                    setMesh(step, nDim);
                    Series series = Series(filename, Access::CREATE, MPI_COMM_WORLD);
                    series.setMeshesPath( "fields" );
                    store(series, step);
                }
            }
        }

        { // group based
            std::ostringstream s;
            s << "../samples/8a_parallel_"<<nDim<<"D"<<balance<<m_Backend;
            std::string filename = s.str();

            {
                std::string tag = "Writing: "+filename ;
                benchmark::Timer kk( tag.c_str(), m_MPIRank, program_start );

                Series series = Series(filename, Access::CREATE, MPI_COMM_WORLD);
                series.setMeshesPath( "fields" );

                for( int step = 1; step <= m_Steps; step++ ) {
                    store(series, step);
                }
            }
        }
    }


    /** Write meshes
    *
    * @param series     Input, openPMD series
    * @param step       Input, iteration step
    * @param fieldName  Input, mesh name
    * @param compName   Input, component of mesh
    */
    void
    storeMesh(Series& series, int step, const std::string& fieldName, const std::string& compName)
    {
        MeshRecordComponent compA = series.iterations[step].meshes[fieldName][compName];

        Datatype datatype = determineDatatype< double >();
        Dataset dataset = Dataset( datatype, m_GlobalMesh );

        compA.resetDataset( dataset );

        auto nBlocks = getNumBlocks();

        for ( unsigned int n=0; n<nBlocks; n++ )
        {
            Extent meshExtent;
            Offset meshOffset;
            auto const blockSize = getNthMeshExtent(n, meshOffset, meshExtent);
            if( blockSize > 0ul ) {
                double const value = double(1.0*n + 0.0001*step);
                auto A = createData<double>( blockSize, value, false ) ;
                compA.storeChunk( A, meshOffset, meshExtent );
            }
        }
    }

    /** Write particles (always 1D)
     *
     * @param ParticleSpecies    Input
     * @param step               Iteration step
     */
    void
    storeParticles( ParticleSpecies& currSpecies,  int& step )
    {
        currSpecies.setAttribute( "particleSmoothing", "none" );
        currSpecies.setAttribute( "openPMD_STEP", step );
        currSpecies.setAttribute( "multiplier", m_Ratio );

        auto np = getTotalNumParticles();
        auto const intDataSet = openPMD::Dataset(openPMD::determineDatatype< uint64_t >(), {np});
        auto const realDataSet = openPMD::Dataset(openPMD::determineDatatype< double >(), {np});
        currSpecies["id"][RecordComponent::SCALAR].resetDataset( intDataSet );
        currSpecies["charge"][RecordComponent::SCALAR].resetDataset( realDataSet );

        currSpecies["position"]["x"].resetDataset( realDataSet );

        currSpecies["positionOffset"]["x"].resetDataset( realDataSet );
        currSpecies["positionOffset"]["x"].makeConstant( 0. );

        auto nBlocks = getNumBlocks();

        for ( unsigned int n=0; n<nBlocks; n++ )
        {
            unsigned long offset=0, count=0;
            getNthParticleExtent(n, offset, count);
            if( count > 0ul ) {
                auto ids = createData<uint64_t>( count, offset, true ) ;
                currSpecies["id"][RecordComponent::SCALAR].storeChunk(ids, {offset}, {count});

                auto charges = createData<double>(count, 0.001*step, false) ;
                currSpecies["charge"][RecordComponent::SCALAR].storeChunk(charges,
                                    {offset}, {count});

                auto mx = createData<double>(count, 0.0003*step, false) ;
                currSpecies["position"]["x"].storeChunk(mx,
                          {offset}, {count});
            }
        }
    } // storeParticles


    /** Write a Series
    *
    * @param Series   input
    * @param step     iteration step
    *
    */
    void store( Series& series, int step )
    {
        std::string comp_alpha = "alpha";
        std::string fieldName1 = "E";
        storeMesh(series, step, fieldName1, comp_alpha);

        std::string fieldName2 = "B";
        storeMesh(series, step, fieldName2, comp_alpha);

        std::string field_rho = "rho";
        std::string scalar = openPMD::MeshRecordComponent::SCALAR;
        storeMesh(series, step, field_rho, scalar);

        ParticleSpecies& currSpecies = series.iterations[step].particles["ion"];
        storeParticles(currSpecies, step);

        series.iterations[step].close();
    }

    /** Setup a Mesh
    *
    * setup the mesh according to dimension
    * when dim=2, second dimension is 128
    * call this function before writing series
    *
    * @param step    iteration step
    * @param nDim    num dimension
    */
    void setMesh(int step, int nDim=1)
    {
        if (2 < nDim)
          return;

        if (1 == nDim)
          m_GlobalMesh = {m_Bulk * m_MPISize};
        if (2 == nDim)
          m_GlobalMesh = {m_Bulk * m_MPISize, 128};

        setBlockDistributionInRank(step);
    }

    /**
     * get number of blocks
     * related to setMesh()
     */
    unsigned int getNumBlocks()
    {
        if (1 == m_GlobalMesh.size())
            return m_InRankDistribution.size();
        if (2 == m_GlobalMesh.size())
            return m_InRankDistribution.size() * 2;

        return 0;
    }

    /**
    * Returns offset/count of the Nth mesh block in a rank
    *
    * @param[in] n         which block
    * @param[out] offset   Return
    * @param[out] count    Return
    *
    */
    unsigned long
    getNthMeshExtent( unsigned int n, Offset& offset, Extent& count )
    {
        if (n >= getNumBlocks())
          return 0;

        if (1 == m_GlobalMesh.size())
          {
            offset = {m_InRankDistribution[n].first};
            count  = {m_InRankDistribution[n].second};
            return count[0];
          }

        if (2 == m_GlobalMesh.size())
          {
            auto mid = m_GlobalMesh[1]/2;
            auto rest = m_GlobalMesh[1] - mid;
            auto ss = m_InRankDistribution.size();
            if (n < ss)
            {
               offset = {m_InRankDistribution[n].first, 0};
               count  = {m_InRankDistribution[n].second, mid};
            }
            else
            { // ss <= n << 2*ss
              offset = {m_InRankDistribution[n-ss].first, rest};
              count  = {m_InRankDistribution[n-ss].second, rest};
            }

            return count[0] * count[1];
         }

        return 0;
    }


    /**
    * Return total number of particles
    *   set to be a multiple of mesh size
    */
    unsigned long
    getTotalNumParticles()
    {
        unsigned long result = m_Ratio;

        for (unsigned int  i=0; i<m_GlobalMesh.size(); i++)
          result *= m_GlobalMesh[i];

        return result;
    }

    /**
    * Returns number of particles on a block in a rank
    */
    void
    getNthParticleExtent( unsigned int n, unsigned long& offset, unsigned long& count )
    {
        if ( n >= getNumBlocks() )
            return;

        if ( 1 == m_GlobalMesh.size() )
        {
            offset =  m_InRankDistribution[n].first  * m_Ratio ;
            count  =  m_InRankDistribution[n].second * m_Ratio ;
            return;
        }

        if ( 2 == m_GlobalMesh.size() )
        {
            auto mid = m_GlobalMesh[1]/2;
            auto rest = m_GlobalMesh[1] - mid;
            auto ss = m_InRankDistribution.size();

            auto rankPatch = m_Bulk * mid * m_MPIRank * m_Ratio;
            if ( n < ss)
            {
                offset = rankPatch + m_InRankDistribution[n].first  * mid * m_Ratio;
                count  = m_InRankDistribution[n].second * mid * m_Ratio;
            }
            else
            {
                auto firstHalf = m_Bulk  * mid * m_Ratio + rankPatch;
                offset =  m_InRankDistribution[n - ss].first  * rest * m_Ratio + firstHalf;
                count  =  m_InRankDistribution[n - ss].second * rest * m_Ratio;
            }
        }
    }

    int m_MPISize = 1; //!< MPI communicator size
    int m_MPIRank = 0; //!< MPI rank
    unsigned long m_Bulk = 1000ul; //!< num of elements at long dimension
    /** number of subdivisions for the elements
    *
    * note that with h5collect mode, m_Seg must be 1
    */
    unsigned int m_Seg = 1;
    int m_Steps = 1;   //!< num of iterations
    std::string m_Backend = ".bp"; //!< I/O backend by file ending
    bool m_Unbalance = false;      //! load is different among processors

    int m_Ratio = 1; //! particle:mesh ratio

    Extent m_GlobalMesh; //! the global mesh grid
    /** partition the workload on this rank along the long dimension (default x)
     *  see setBlockDistributionInRank()
     */
    std::vector<std::pair<unsigned long, unsigned long>> m_InRankDistribution;
};


/** Benchmark entry point
 *
 *  positional runtime arguments:
 *  - num: ...?
 *  - bulk: num of elements at long dimension
 *  - seg: subdivisions for the elements
 *  - steps: number of steps to create
 */
int
main( int argc, char *argv[] )
{
    MPI_Init( &argc, &argv );
    TestInput input;

    MPI_Comm_size( MPI_COMM_WORLD, &input.m_MPISize );
    MPI_Comm_rank( MPI_COMM_WORLD, &input.m_MPIRank );

    benchmark::Timer g( "  Main  ", input.m_MPIRank, program_start );

    if( argc >= 2 )
    {
        int num = atoi( argv[1] ) ;

        if (num > 10)
            input.m_Unbalance = true;

        if ( num <=  0)
            num = 1;

        input.m_Ratio = (num-1) % 10 + 1;
    }

    if( argc >= 3 )
        input.m_Bulk = strtoul( argv[2], NULL, 0 );

    if( argc >= 4 )
        input.m_Seg = atoi( argv[3] );

    if( argc >= 5 )
        input.m_Steps = atoi( argv[4] );


    auto const backends = getBackends();
    for( auto const & which: backends )
    {
        input.m_Backend = which;
        input.run(1);
        input.run(2);
    }

    MPI_Finalize();

    return 0;
}