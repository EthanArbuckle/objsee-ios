//
//  blocks.h
//  libobjsee
//
//  Created by Ethan Arbuckle on 12/10/24.
//

#ifndef BLOCKS_H
#define BLOCKS_H

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>

/**
 * @brief Describe a block's type signature in a human-readable format
 *
 * @param block The block to describe
 * @return The description of the block
 * @note The returned string is malloc'd and must be freed by the caller
 */
kern_return_t get_block_description(id block, char **out_description);


#endif /* BLOCKS_H */
