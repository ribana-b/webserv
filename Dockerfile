FROM xianpengshen/clang-tools:14

RUN apt-get update && apt-get install -y make

WORKDIR /app

COPY . .

CMD [ "make", "-n" ]
