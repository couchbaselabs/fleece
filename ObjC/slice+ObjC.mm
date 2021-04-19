//
// slice+ObjC.mm
//
// Copyright (c) 2021 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "fleece/slice.hh"
#include <Foundation/Foundation.h>

namespace fleece {

    pure_slice::pure_slice(NSData* data) noexcept
    :pure_slice((__bridge CFDataRef)data)
    { }


    NSData* pure_slice::copiedNSData() const {
        return buf ? [[NSData alloc] initWithBytes: buf length: size] : nil;
    }


    NSData* pure_slice::uncopiedNSData() const {
        if (!buf)
            return nil;
        return [[NSData alloc] initWithBytesNoCopy: (void*)buf length: size freeWhenDone: NO];
    }


    NSString* pure_slice::asNSString() const {
        return CFBridgingRelease(createCFString());
    }


    alloc_slice::alloc_slice(NSData *data)
    :alloc_slice((__bridge CFDataRef)data)
    { }


    NSData* alloc_slice::uncopiedNSData() const {
        return CFBridgingRelease(createCFData());
    }

}
