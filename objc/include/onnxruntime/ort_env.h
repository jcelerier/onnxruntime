// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#import <Foundation/Foundation.h>

/**
 * The ORT environment.
 */
@interface ORTEnv : NSObject

/**
 * Creates an ORT Environment.
 *
 * @param error Optional error information set if an error occurs.
 * @return The instance, or nil if an error occurs.
 */
- (instancetype)initWithError:(NSError**)error;

@end
