FROM squidfunk/mkdocs-material
RUN pip install mkdoxy && \
    apk --update --no-cache add doxygen
WORKDIR /docs
COPY ./mkdocs/mkdocs.yml .
COPY ./mkdocs/docs ./docs
ENTRYPOINT []