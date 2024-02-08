FROM ubuntu:22.04 AS build

WORKDIR /src/src
RUN apt update && apt upgrade -y
RUN apt install -y libgtk-4-dev libasound2-dev

COPY . /src/

RUN make

FROM scratch AS export
COPY --from=build /src/src/alsa-scarlett-gui .

