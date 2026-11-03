#
# Makefile
# Computer Networking Programing Assignments
#
#  Created by Phillip Romig on 4/3/12.
#  Copyright 2012 Colorado School of Mines. All rights reserved.
#

USERNAME = cnubel

CXX = g++
LD = g++
CXXFLAGS = -g3 -std=c++17
LDFLAGS = 

#
# You should be able to add object files here without changing anything else
#
TARGET = traceroute
OBJ_FILES = ${TARGET}.o
INC_FILES = ${TARGET}.h

#
# Any libraries we might need.
#
#LIBRARYS = -lbsd -lboost_regex -lboost_log_setup -lboost_log -lboost_thread -lboost_system -lpthread -lboost_filesystem

${TARGET}: ${OBJ_FILES}
	${LD} ${LDFLAGS} ${OBJ_FILES} -o $@ ${LIBRARYS}

%.o : %.cpp ${INC_FILES}
	${CXX} -c ${CXXFLAGS} -o $@ $<

#
# Please remember not to submit objects or binarys.
#
clean:
	rm -f core ${TARGET} ${OBJ_FILES}

#
# This might work to create the submission tarball in the formal I asked for.
#
#
# This might work to create the submission tarball in the formal I asked for.
#
submit:
	rm -f core ${TARGET} ${OBJ_FILES}
	mkdir ${USERNAME}
	cp Makefile README.txt *.h *.cpp ${USERNAME}
	tar zcf ${USERNAME}.tgz ${USERNAME}
