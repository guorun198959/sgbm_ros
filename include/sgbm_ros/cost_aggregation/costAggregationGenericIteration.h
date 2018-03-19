#ifndef COSTAGGREGATIONGENERICITERATION_H
#define COSTAGGREGATIONGENERICITERATION_H

#include "../configuration.h"
#include "../util.h"

#define ITER_NORMAL 1
#define ITER_COPY 0

#define DIR_UPDOWN 0
#define DIR_DOWNUP 1
#define DIR_LEFTRIGHT 2
#define DIR_RIGHTLEFT 3

#define MIN_COMPUTE 0
#define MIN_NOCOMPUTE 1

template< class T, int iter_type, int min_type, int dir_type, bool first_iteration, bool recompute, bool join_dispcomputation >
__device__ __forceinline__ void
CostAggregationGenericIteration( int index,
                                 int index_im,
                                 int col,
                                 uint32_t* old_values,
                                 int* old_value1,
                                 int* old_value2,
                                 int* old_value3,
                                 int* old_value4,
                                 uint32_t* min_cost,
                                 uint32_t* min_cost_p2,
                                 uint8_t* d_cost,
                                 uint8_t* d_L,
                                 const int p1_vector,
                                 const int p2_vector,
                                 const T* _d_transform0,
                                 const T* _d_transform1,
                                 const int lane,
                                 const int MAX_PAD,
                                 const int dis,
                                 T* rp0,
                                 T* rp1,
                                 T* rp2,
                                 T* rp3,
                                 uint8_t* __restrict__ d_disparity,
                                 const uint8_t* d_L0,
                                 const uint8_t* d_L1,
                                 const uint8_t* d_L2,
                                 const uint8_t* d_L3,
                                 const uint8_t* d_L4,
                                 const uint8_t* d_L5,
                                 const uint8_t* d_L6 )
{
    const T __restrict__* d_transform0 = _d_transform0;
    const T __restrict__* d_transform1 = _d_transform1;
    uint32_t costs, next_dis, prev_dis;

    if ( iter_type == ITER_NORMAL )
    {
        // First shuffle
        int prev_dis1 = shfl_up_32( *old_value4, 1 );
        if ( lane == 0 )
        {
            prev_dis1 = MAX_PAD;
        }

        // Second shuffle
        int next_dis4 = shfl_down_32( *old_value1, 1 );
        if ( lane == 31 )
        {
            next_dis4 = MAX_PAD;
        }

        // Shift + rotate
        // next_dis = __funnelshift_r(next_dis4, *old_values, 8);
        next_dis = __byte_perm( *old_values, next_dis4, 0x4321 );
        prev_dis = __byte_perm( *old_values, prev_dis1, 0x2104 );

        next_dis = next_dis + p1_vector;
        prev_dis = prev_dis + p1_vector;
    }
    if ( recompute )
    {
        const int dif = col - dis;
        if ( dir_type == DIR_LEFTRIGHT )
        {
            if ( lane == 0 )
            {
                // lane = 0 is dis = 0, no need to subtract dis
                *rp0 = d_transform1[index_im];
            }
        }
        else if ( dir_type == DIR_RIGHTLEFT )
        {
            // First iteration, load D pixels
            if ( first_iteration )
            {
                const uint4 right
                = reinterpret_cast< const uint4* >( &d_transform1[index_im - dis - 3] )[0];
                *rp3 = right.x;
                *rp2 = right.y;
                *rp1 = right.z;
                *rp0 = right.w;
            }
            else if ( lane == 31 && dif >= 3 )
            {
                *rp3 = d_transform1[index_im - dis - 3];
            }
        }
        else
        {
            /*
                    __shared__ T right_p[MAX_DISPARITY+32];
                    const int warp_id = threadIdx.x / WARP_SIZE;
                    if(warp_id < 5) {
                        const int block_imindex = index_im - warp_id + 32;
                        const int rp_index = warp_id*WARP_SIZE+lane;
                        const int col_cpy = col-warp_id+32;
                        right_p[rp_index] = ((col_cpy-(159-rp_index)) >= 0) ?
               ld_gbl_cs(&d_transform1[block_imindex-(159-rp_index)]) : 0;
                    }*/

            __shared__ T right_p[128 + 32];
            const int warp_id       = threadIdx.x / WARP_SIZE;
            const int block_imindex = index_im - warp_id + 2;
            const int rp_index      = warp_id * WARP_SIZE + lane;
            const int col_cpy       = col - warp_id + 2;

            right_p[rp_index] = ( ( col_cpy - ( 129 - rp_index ) ) >= 0 ) ?
                                d_transform1[block_imindex - ( 129 - rp_index )] :
                                0;
            right_p[rp_index + 64] = ( ( col_cpy - ( 129 - rp_index - 64 ) ) >= 0 ) ?
                                     d_transform1[block_imindex - ( 129 - rp_index - 64 )] :
                                     0;

            // right_p[rp_index+128] =
            // ld_gbl_cs(&d_transform1[block_imindex-(129-rp_index-128)]);
            if ( warp_id == 0 )
            {
                right_p[128 + lane] = ld_gbl_cs( &d_transform1[block_imindex - ( 129 - lane )] );
            }
            __syncthreads( );

            const int px = MAX_DISPARITY + warp_id - dis - 1;

            *rp0 = right_p[px];
            *rp1 = right_p[px - 1];
            *rp2 = right_p[px - 2];
            *rp3 = right_p[px - 3];
        }
        const T left_pixel = d_transform0[index_im];
        *old_value1        = popcount( left_pixel ^ *rp0 );
        *old_value2        = popcount( left_pixel ^ *rp1 );
        *old_value3        = popcount( left_pixel ^ *rp2 );
        *old_value4        = popcount( left_pixel ^ *rp3 );
        if ( iter_type == ITER_COPY )
        {
            *old_values = uchars_to_uint32( *old_value1, *old_value2, *old_value3, *old_value4 );
        }
        else
        {
            costs = uchars_to_uint32( *old_value1, *old_value2, *old_value3, *old_value4 );
        }
        // Prepare for next iteration
        if ( dir_type == DIR_LEFTRIGHT )
        {
            *rp3 = shfl_up_32( *rp3, 1 );
        }
        else if ( dir_type == DIR_RIGHTLEFT )
        {
            *rp0 = shfl_down_32( *rp0, 1 );
        }
    }
    else
    {
        if ( iter_type == ITER_COPY )
        {
            *old_values = ld_gbl_ca( reinterpret_cast< const uint32_t* >( &d_cost[index] ) );
        }
        else
        {
            costs = ld_gbl_ca( reinterpret_cast< const uint32_t* >( &d_cost[index] ) );
        }
    }

    if ( iter_type == ITER_NORMAL )
    {
        const uint32_t min1     = __vminu4( *old_values, prev_dis );
        const uint32_t min2     = __vminu4( next_dis, *min_cost_p2 );
        const uint32_t min_prev = __vminu4( min1, min2 );
        *old_values             = costs + ( min_prev - *min_cost );
    }
    if ( iter_type == ITER_NORMAL || !recompute )
    {
        uint32_to_uchars( *old_values, old_value1, old_value2, old_value3, old_value4 );
    }

    if ( join_dispcomputation )
    {
        const uint32_t L0_costs = *( ( uint32_t* )( d_L0 + index ) );
        const uint32_t L1_costs = *( ( uint32_t* )( d_L1 + index ) );
        const uint32_t L2_costs = *( ( uint32_t* )( d_L2 + index ) );
#if PATH_AGGREGATION == 8
        const uint32_t L3_costs = *( ( uint32_t* )( d_L3 + index ) );
        const uint32_t L4_costs = *( ( uint32_t* )( d_L4 + index ) );
        const uint32_t L5_costs = *( ( uint32_t* )( d_L5 + index ) );
        const uint32_t L6_costs = *( ( uint32_t* )( d_L6 + index ) );
#endif

        int l0_x, l0_y, l0_z, l0_w;
        int l1_x, l1_y, l1_z, l1_w;
        int l2_x, l2_y, l2_z, l2_w;
#if PATH_AGGREGATION == 8
        int l3_x, l3_y, l3_z, l3_w;
        int l4_x, l4_y, l4_z, l4_w;
        int l5_x, l5_y, l5_z, l5_w;
        int l6_x, l6_y, l6_z, l6_w;
#endif

        uint32_to_uchars( L0_costs, &l0_x, &l0_y, &l0_z, &l0_w );
        uint32_to_uchars( L1_costs, &l1_x, &l1_y, &l1_z, &l1_w );
        uint32_to_uchars( L2_costs, &l2_x, &l2_y, &l2_z, &l2_w );
#if PATH_AGGREGATION == 8
        uint32_to_uchars( L3_costs, &l3_x, &l3_y, &l3_z, &l3_w );
        uint32_to_uchars( L4_costs, &l4_x, &l4_y, &l4_z, &l4_w );
        uint32_to_uchars( L5_costs, &l5_x, &l5_y, &l5_z, &l5_w );
        uint32_to_uchars( L6_costs, &l6_x, &l6_y, &l6_z, &l6_w );
#endif

#if PATH_AGGREGATION == 8
        const uint16_t val1 = l0_x + l1_x + l2_x + l3_x + l4_x + l5_x + l6_x + *old_value1;
        const uint16_t val2 = l0_y + l1_y + l2_y + l3_y + l4_y + l5_y + l6_y + *old_value2;
        const uint16_t val3 = l0_z + l1_z + l2_z + l3_z + l4_z + l5_z + l6_z + *old_value3;
        const uint16_t val4 = l0_w + l1_w + l2_w + l3_w + l4_w + l5_w + l6_w + *old_value4;
#else
        const uint16_t val1 = l0_x + l1_x + l2_x + *old_value1;
        const uint16_t val2 = l0_y + l1_y + l2_y + *old_value2;
        const uint16_t val3 = l0_z + l1_z + l2_z + *old_value3;
        const uint16_t val4 = l0_w + l1_w + l2_w + *old_value4;
#endif
        int min_idx1  = dis;
        uint16_t min1 = val1;
        if ( val1 > val2 )
        {
            min1     = val2;
            min_idx1 = dis + 1;
        }

        int min_idx2  = dis + 2;
        uint16_t min2 = val3;
        if ( val3 > val4 )
        {
            min2     = val4;
            min_idx2 = dis + 3;
        }

        uint16_t minval = min1;
        int min_idx     = min_idx1;
        if ( min1 > min2 )
        {
            minval  = min2;
            min_idx = min_idx2;
        }

        const int min_warpindex = warpReduceMinIndex( minval, min_idx );
        if ( lane == 0 )
        {
            d_disparity[index_im] = min_warpindex;
        }
    }
    else
    {
        st_gbl_cs( reinterpret_cast< uint32_t* >( &d_L[index] ), *old_values );
    }
    if ( min_type == MIN_COMPUTE )
    {
        int min_cost_scalar = min( min( *old_value1, *old_value2 ), min( *old_value3, *old_value4 ) );
        *min_cost           = uchar_to_uint32( warpReduceMin( min_cost_scalar ) );
        *min_cost_p2 = *min_cost + p2_vector;
    }
}

#endif // COSTAGGREGATIONGENERICITERATION_H
