FROM gcc:13.2.0

RUN apt-get update && \
    apt-get install -y flex bison && \
    apt-get clean

WORKDIR /typeeasy

COPY src/ .                 
COPY typeeasycode /typeeasy/typeeasycode

RUN make clean && make

ENTRYPOINT ["./typeeasy"]



