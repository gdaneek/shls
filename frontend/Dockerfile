FROM nginx:1.28-alpine

COPY nginx.conf /etc/nginx/nginx.conf
COPY index.html style.css app.js /usr/share/nginx/html/

USER 101:101
EXPOSE 8080
ENTRYPOINT ["nginx", "-g", "daemon off;"]
